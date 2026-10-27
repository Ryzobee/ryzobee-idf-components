/** @file yoke_rgbw.c */
#include "yoke_rgbw.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"

#define YOKE_RGBW_RMT_RESOLUTION_HZ 10000000U
#define YOKE_RGBW_RESET_US 300U

static const char *TAG = "yoke_rgbw";

struct yoke_rgbw {
    rmt_channel_handle_t channel;
    rmt_encoder_handle_t encoder;
    uint8_t *pixels;
    size_t pixels_size;
    rootmaker_port_t port;
    bool port_held;
    bool channel_enabled;
    bool stopping;
    gpio_num_t pins[1];
};

/* Lifecycle calls are serialized by the application. */
static yoke_rgbw_handle_t s_failed_init;

static const rmt_symbol_word_t s_zero = {
    .level0 = 1,
    .duration0 = 3,
    .level1 = 0,
    .duration1 = 9,
};
static const rmt_symbol_word_t s_one = {
    .level0 = 1,
    .duration0 = 9,
    .level1 = 0,
    .duration1 = 3,
};
static const rmt_symbol_word_t s_reset = {
    /* A conservative latch interval, entirely low, including back-to-back writes. */
    .level0 = 0,
    .duration0 = (YOKE_RGBW_RMT_RESOLUTION_HZ / 1000000U) * YOKE_RGBW_RESET_US / 2U,
    .level1 = 0,
    .duration1 = (YOKE_RGBW_RMT_RESOLUTION_HZ / 1000000U) * YOKE_RGBW_RESET_US / 2U,
};

static size_t yoke_rgbw_encode(const void *data, size_t data_size, size_t symbols_written, size_t symbols_free, rmt_symbol_word_t *symbols, bool *done,
                               void *arg) {
    (void)arg;
    if (symbols_free < 8)
        return 0;

    size_t byte_index = symbols_written / 8;
    const uint8_t *bytes = data;
    if (byte_index >= data_size) {
        symbols[0] = s_reset;
        *done = true;
        return 1;
    }

    for (uint8_t mask = 0x80U, index = 0; mask != 0U; mask >>= 1U) {
        symbols[index++] = (bytes[byte_index] & mask) ? s_one : s_zero;
    }
    return 8;
}

static esp_err_t yoke_rgbw_transmit(yoke_rgbw_handle_t rgbw) {
    const rmt_transmit_config_t config = { .loop_count = 0 };
    esp_err_t ret = rmt_transmit(rgbw->channel, rgbw->encoder, rgbw->pixels, rgbw->pixels_size, &config);
    return ret == ESP_OK ? rmt_tx_wait_all_done(rgbw->channel, portMAX_DELAY) : ret;
}

yoke_rgbw_handle_t yoke_rgbw_init(rootmaker_port_t port, uint16_t led_num) {
    if (s_failed_init != NULL) {
        esp_err_t ret = yoke_rgbw_deinit(s_failed_init);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "previous initialization cleanup still failing: %s", esp_err_to_name(ret));
            return NULL;
        }
        s_failed_init = NULL;
    }
    gpio_num_t data_gpio;
    if (led_num == 0U || rootmaker_get_port_pin(&port, 0, &data_gpio) != ESP_OK) {
        ESP_LOGE(TAG, "invalid configuration");
        return NULL;
    }

    yoke_rgbw_handle_t rgbw = calloc(1, sizeof(struct yoke_rgbw));
    if (rgbw == NULL) {
        ESP_LOGE(TAG, "no mem");
        return NULL;
    }
    rgbw->pins[0] = data_gpio;
    rgbw->port = (rootmaker_port_t){ .type = port.type, .pin_num = 1, .pins = rgbw->pins };
    rgbw->pixels_size = (size_t)led_num * 4U;

    esp_err_t ret = rootmaker_use_port(&rgbw->port);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "port in use: %s", esp_err_to_name(ret));
        free(rgbw);
        return NULL;
    }
    rgbw->port_held = true;

    rgbw->pixels = calloc(rgbw->pixels_size, sizeof(*rgbw->pixels));
    if (rgbw->pixels == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }

    rmt_tx_channel_config_t channel_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = data_gpio,
        .mem_block_symbols = 64,
        .resolution_hz = YOKE_RGBW_RMT_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    ret = rmt_new_tx_channel(&channel_config, &rgbw->channel);
    if (ret != ESP_OK)
        goto fail;

    rmt_simple_encoder_config_t encoder_config = { .callback = yoke_rgbw_encode };
    ret = rmt_new_simple_encoder(&encoder_config, &rgbw->encoder);
    if (ret != ESP_OK)
        goto fail;

    ret = rmt_enable(rgbw->channel);
    if (ret != ESP_OK)
        goto fail;
    rgbw->channel_enabled = true;
    return rgbw;

fail:
    ESP_LOGE(TAG, "initialization failed: %s", esp_err_to_name(ret));
    ret = yoke_rgbw_deinit(rgbw);
    if (ret != ESP_OK) {
        s_failed_init = rgbw;
        ESP_LOGE(TAG, "initialization cleanup failed: %s; next init will retry cleanup", esp_err_to_name(ret));
    }
    return NULL;
}

esp_err_t yoke_rgbw_deinit(yoke_rgbw_handle_t rgbw) {
    if (rgbw == NULL)
        return ESP_ERR_INVALID_ARG;
    rgbw->stopping = true;
    esp_err_t ret;
    if (rgbw->channel_enabled) {
        ret = rmt_disable(rgbw->channel);
        if (ret != ESP_OK)
            return ret;
        rgbw->channel_enabled = false;
    }
    if (rgbw->channel != NULL) {
        ret = rmt_del_channel(rgbw->channel);
        if (ret != ESP_OK)
            return ret;
        rgbw->channel = NULL;
    }
    if (rgbw->encoder != NULL) {
        ret = rmt_del_encoder(rgbw->encoder);
        if (ret != ESP_OK)
            return ret;
        rgbw->encoder = NULL;
    }
    if (rgbw->port_held) {
        ret = rootmaker_release_port(&rgbw->port);
        if (ret != ESP_OK)
            return ret;
        rgbw->port_held = false;
    }
    free(rgbw->pixels);
    free(rgbw);
    return ESP_OK;
}

esp_err_t yoke_rgbw_set_all(yoke_rgbw_handle_t rgbw, uint8_t red, uint8_t green, uint8_t blue, uint8_t white) {
    if (rgbw == NULL)
        return ESP_ERR_INVALID_ARG;
    if (rgbw->stopping || !rgbw->channel_enabled || rgbw->encoder == NULL || rgbw->pixels == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t index = 0; index < rgbw->pixels_size; index += 4U) {
        rgbw->pixels[index] = green;
        rgbw->pixels[index + 1U] = red;
        rgbw->pixels[index + 2U] = blue;
        rgbw->pixels[index + 3U] = white;
    }
    return yoke_rgbw_transmit(rgbw);
}

esp_err_t yoke_rgbw_set_pixel(yoke_rgbw_handle_t rgbw, uint16_t index, uint8_t red, uint8_t green, uint8_t blue, uint8_t white) {
    if (rgbw == NULL)
        return ESP_ERR_INVALID_ARG;
    if (rgbw->stopping || !rgbw->channel_enabled || rgbw->encoder == NULL || rgbw->pixels == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((size_t)index >= rgbw->pixels_size / 4U) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t pixel_offset = (size_t)index * 4U;
    rgbw->pixels[pixel_offset] = green;
    rgbw->pixels[pixel_offset + 1U] = red;
    rgbw->pixels[pixel_offset + 2U] = blue;
    rgbw->pixels[pixel_offset + 3U] = white;
    return yoke_rgbw_transmit(rgbw);
}
