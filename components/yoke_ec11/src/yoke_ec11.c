/** @file yoke_ec11.c */
#include "yoke_ec11.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#define TAG "yoke_ec11"

#define YOKE_REG_ID_HIGH 0x00U
#define YOKE_REG_KEY 0x02U
#define YOKE_REG_KEY_TIME 0x03U
#define YOKE_REG_COUNT_HIGH 0x04U
#define YOKE_REG_DIFF_HIGH 0x06U
#define YOKE_REG_LED1_RED 0x08U
#define YOKE_REG_CTRL1 0x0EU
#define YOKE_REG_CTRL2 0x0FU

#define YOKE_KEY_PRESSED (1U << 7)
#define YOKE_KEY_COUNT_MASK 0x0FU

struct yoke_ec11 {
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;
    rootmaker_port_t port;
    gpio_num_t pins[2];
    uint8_t device_address;
    uint32_t scl_speed_hz;
    uint32_t timeout_ms;
    bool bus_ref_held;
    bool initialized;
    bool stopping;
    bool address_held;
};

/* Lifecycle calls are serialized by the application. */
static yoke_ec11_handle_t s_failed_init;

static bool yoke_is_address_valid(uint8_t address) {
    return address >= 0x08U && address <= 0x77U;
}

static esp_err_t yoke_check(yoke_ec11_handle_t yoke) {
    if (yoke == NULL)
        return ESP_ERR_INVALID_ARG;
    if (!yoke->initialized || yoke->stopping || yoke->dev_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t yoke_read(yoke_ec11_handle_t yoke, uint8_t reg, uint8_t *data, size_t len) {
    if (data == NULL || len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = yoke_check(yoke);
    if (ret != ESP_OK) {
        return ret;
    }
    return i2c_master_transmit_receive(yoke->dev_handle, &reg, 1U, data, len, yoke->timeout_ms);
}

static esp_err_t yoke_write(yoke_ec11_handle_t yoke, uint8_t reg, const uint8_t *data, size_t len) {
    if (data == NULL || len == 0U || len > 6U) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = yoke_check(yoke);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t tx[7];
    tx[0] = reg;
    memcpy(&tx[1], data, len);
    return i2c_master_transmit(yoke->dev_handle, tx, len + 1U, yoke->timeout_ms);
}

static esp_err_t yoke_add_device(yoke_ec11_handle_t yoke, uint8_t address) {
    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = yoke->scl_speed_hz,
    };
    esp_err_t ret = i2c_master_bus_add_device(yoke->bus_handle, &device_config, &yoke->dev_handle);
    if (ret == ESP_OK) {
        yoke->device_address = address;
    }
    return ret;
}

static bool yoke_config_is_valid(rootmaker_port_t port, const yoke_ec11_config_t *config) {
    if (config == NULL || !yoke_is_address_valid(config->device_address) || config->scl_speed_hz == 0U || config->timeout_ms == 0U ||
        config->timeout_ms > INT_MAX) {
        return false;
    }
    gpio_num_t sda;
    gpio_num_t scl;
    return rootmaker_get_port_pins(&port, &sda, &scl) == ESP_OK && sda != scl;
}

yoke_ec11_handle_t yoke_ec11_init_with_config(rootmaker_port_t port, const yoke_ec11_config_t *config) {
    if (s_failed_init != NULL) {
        esp_err_t ret = yoke_ec11_deinit(s_failed_init);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "previous initialization cleanup still failing: %s", esp_err_to_name(ret));
            return NULL;
        }
        s_failed_init = NULL;
    }
    if (!yoke_config_is_valid(port, config)) {
        ESP_LOGE(TAG, "invalid configuration");
        return NULL;
    }

    yoke_ec11_handle_t yoke = calloc(1, sizeof(struct yoke_ec11));
    if (yoke == NULL) {
        ESP_LOGE(TAG, "no mem");
        return NULL;
    }
    yoke->pins[0] = port.pins[0];
    yoke->pins[1] = port.pins[1];
    yoke->port = (rootmaker_port_t){ .type = port.type, .pin_num = 2, .pins = yoke->pins };
    yoke->device_address = config->device_address;
    yoke->scl_speed_hz = config->scl_speed_hz;
    yoke->timeout_ms = config->timeout_ms;

    esp_err_t ret = rootmaker_new_i2c_bus(&yoke->port, &yoke->bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "new i2c bus failed: %s", esp_err_to_name(ret));
        free(yoke);
        return NULL;
    }
    yoke->bus_ref_held = true;
    ret = rootmaker_claim_i2c_address(yoke->bus_handle, yoke->device_address);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C address already bound or unavailable: %s", esp_err_to_name(ret));
        goto fail;
    }
    yoke->address_held = true;
    ret = yoke_add_device(yoke, config->device_address);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "add device failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    yoke->initialized = true;

    uint16_t device_id = 0;
    ret = yoke_ec11_read_device_id(yoke, &device_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG,
                 "device ID read failed: address=0x%02X SDA=GPIO%d SCL=GPIO%d: %s (0x%x). "
                 "Check module power, common GND, SDA/SCL order and pull-ups",
                 yoke->device_address, yoke->port.pins[0], yoke->port.pins[1], esp_err_to_name(ret), ret);
        goto fail;
    }
    if (device_id != YOKE_EC11_DEVICE_ID) {
        ESP_LOGE(TAG, "unexpected device ID at address 0x%02X: got 0x%04X, expected 0x%04X", yoke->device_address, device_id, YOKE_EC11_DEVICE_ID);
        goto fail;
    }
    return yoke;

fail:
    ret = yoke_ec11_deinit(yoke);
    if (ret != ESP_OK) {
        s_failed_init = yoke;
        ESP_LOGE(TAG, "initialization cleanup failed: %s; next init will retry cleanup", esp_err_to_name(ret));
    }
    return NULL;
}

yoke_ec11_handle_t yoke_ec11_init(rootmaker_port_t port) {
    yoke_ec11_config_t config = YOKE_EC11_CONFIG_DEFAULT();
    return yoke_ec11_init_with_config(port, &config);
}

esp_err_t yoke_ec11_deinit(yoke_ec11_handle_t yoke) {
    if (yoke == NULL)
        return ESP_ERR_INVALID_ARG;
    yoke->stopping = true;
    yoke->initialized = false;
    esp_err_t ret;
    if (yoke->dev_handle != NULL) {
        ret = i2c_master_bus_rm_device(yoke->dev_handle);
        if (ret != ESP_OK)
            return ret;
        yoke->dev_handle = NULL;
    }
    if (yoke->address_held) {
        ret = rootmaker_release_i2c_address(yoke->bus_handle, yoke->device_address);
        if (ret != ESP_OK)
            return ret;
        yoke->address_held = false;
    }
    if (yoke->bus_ref_held) {
        ret = rootmaker_del_i2c_bus(&yoke->port);
        if (ret != ESP_OK)
            return ret;
        yoke->bus_ref_held = false;
    }
    free(yoke);
    return ESP_OK;
}

esp_err_t yoke_ec11_probe(yoke_ec11_handle_t yoke) {
    esp_err_t ret = yoke_check(yoke);
    return ret == ESP_OK ? i2c_master_probe(yoke->bus_handle, yoke->device_address, yoke->timeout_ms) : ret;
}

esp_err_t yoke_ec11_read_device_id(yoke_ec11_handle_t yoke, uint16_t *device_id) {
    if (device_id == NULL)
        return ESP_ERR_INVALID_ARG;
    uint8_t data[2];
    esp_err_t ret = yoke_read(yoke, YOKE_REG_ID_HIGH, data, sizeof(data));
    if (ret == ESP_OK)
        *device_id = ((uint16_t)data[0] << 8) | data[1];
    return ret;
}

esp_err_t yoke_ec11_read_key(yoke_ec11_handle_t yoke, yoke_ec11_key_t *key) {
    if (key == NULL)
        return ESP_ERR_INVALID_ARG;
    uint8_t data;
    esp_err_t ret = yoke_read(yoke, YOKE_REG_KEY, &data, 1U);
    if (ret == ESP_OK) {
        key->pressed = (data & YOKE_KEY_PRESSED) != 0U;
        key->press_count = data & YOKE_KEY_COUNT_MASK;
    }
    return ret;
}

esp_err_t yoke_ec11_read_key_press_time_ms(yoke_ec11_handle_t yoke, uint16_t *time_ms) {
    if (time_ms == NULL)
        return ESP_ERR_INVALID_ARG;
    uint8_t data;
    esp_err_t ret = yoke_read(yoke, YOKE_REG_KEY_TIME, &data, 1U);
    if (ret == ESP_OK)
        *time_ms = (uint16_t)data * 100U;
    return ret;
}

esp_err_t yoke_ec11_read_encoder_count(yoke_ec11_handle_t yoke, int16_t *count) {
    if (count == NULL)
        return ESP_ERR_INVALID_ARG;
    uint8_t data[2];
    esp_err_t ret = yoke_read(yoke, YOKE_REG_COUNT_HIGH, data, sizeof(data));
    if (ret == ESP_OK)
        *count = (int16_t)(((uint16_t)data[0] << 8) | data[1]);
    return ret;
}

esp_err_t yoke_ec11_read_encoder_diff(yoke_ec11_handle_t yoke, int16_t *diff) {
    if (diff == NULL)
        return ESP_ERR_INVALID_ARG;
    uint8_t data[2];
    esp_err_t ret = yoke_read(yoke, YOKE_REG_DIFF_HIGH, data, sizeof(data));
    if (ret == ESP_OK)
        *diff = (int16_t)(((uint16_t)data[0] << 8) | data[1]);
    return ret;
}

esp_err_t yoke_ec11_set_ws2812(yoke_ec11_handle_t yoke, uint8_t index, yoke_ec11_rgb_t color) {
    if (index > 1U)
        return ESP_ERR_INVALID_ARG;
    uint8_t data[] = { color.red, color.green, color.blue };
    return yoke_write(yoke, (uint8_t)(YOKE_REG_LED1_RED + index * 3U), data, sizeof(data));
}

esp_err_t yoke_ec11_set_ws2812_all(yoke_ec11_handle_t yoke, yoke_ec11_rgb_t led1, yoke_ec11_rgb_t led2) {
    uint8_t data[] = { led1.red, led1.green, led1.blue, led2.red, led2.green, led2.blue };
    return yoke_write(yoke, YOKE_REG_LED1_RED, data, sizeof(data));
}

esp_err_t yoke_ec11_get_ws2812(yoke_ec11_handle_t yoke, uint8_t index, yoke_ec11_rgb_t *color) {
    if (index > 1U || color == NULL)
        return ESP_ERR_INVALID_ARG;
    uint8_t data[3];
    esp_err_t ret = yoke_read(yoke, (uint8_t)(YOKE_REG_LED1_RED + index * 3U), data, sizeof(data));
    if (ret == ESP_OK)
        *color = (yoke_ec11_rgb_t){ .red = data[0], .green = data[1], .blue = data[2] };
    return ret;
}

esp_err_t yoke_ec11_read_ctrl1(yoke_ec11_handle_t yoke, uint8_t *value) {
    return yoke_read(yoke, YOKE_REG_CTRL1, value, 1U);
}

esp_err_t yoke_ec11_write_ctrl1(yoke_ec11_handle_t yoke, uint8_t value) {
    return yoke_write(yoke, YOKE_REG_CTRL1, &value, 1U);
}

esp_err_t yoke_ec11_enter_test_mode(yoke_ec11_handle_t yoke) {
    return yoke_ec11_write_ctrl1(yoke, 0x5AU);
}

esp_err_t yoke_ec11_configure_slave_address(yoke_ec11_handle_t yoke, uint8_t address_low_3bits) {
    if (address_low_3bits > 0x07U)
        return ESP_ERR_INVALID_ARG;
    /* CTRL2 bit layout: 1 !A2 !A1 !A0 0 A2 A1 A0. */
    uint8_t ctrl2 = (uint8_t)(0x80U | (((~address_low_3bits) & 0x07U) << 4) | address_low_3bits);
    return yoke_write(yoke, YOKE_REG_CTRL2, &ctrl2, 1U);
}

esp_err_t yoke_ec11_set_active_address(yoke_ec11_handle_t yoke, uint8_t device_address) {
    esp_err_t check = yoke_check(yoke);
    if (check != ESP_OK)
        return check;
    if (!yoke_is_address_valid(device_address))
        return ESP_ERR_INVALID_ARG;
    if (device_address == yoke->device_address)
        return ESP_OK;

    esp_err_t ret = rootmaker_claim_i2c_address(yoke->bus_handle, device_address);
    if (ret != ESP_OK)
        return ret;
    ret = i2c_master_device_change_address(yoke->dev_handle, device_address, yoke->timeout_ms);
    if (ret == ESP_OK) {
        (void)rootmaker_release_i2c_address(yoke->bus_handle, yoke->device_address);
        yoke->device_address = device_address;
    } else {
        (void)rootmaker_release_i2c_address(yoke->bus_handle, device_address);
    }
    return ret;
}
