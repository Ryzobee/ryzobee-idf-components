#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "ci1302.h"
#include "yoke_voice_io.h"

#include "gmf_ci1302_device_internal.h"
static struct ci1302_device device = {.lock = portMUX_INITIALIZER_UNLOCKED, .quiet = true};

static void receive(ci1302_audio_status_t status, uint8_t *data, uint32_t len) {
    yoke_voice_record_frame_t frame;
    bool valid = status != CI1302_AUDIO_RUNNING || yoke_voice_record_frame_parse(data, len, &frame);
    portENTER_CRITICAL(&device.lock);
    if (status == CI1302_AUDIO_RUNNING || status == CI1302_AUDIO_END)
        device.last_rx_us = esp_timer_get_time();
    if (status == CI1302_AUDIO_END) {
        device.quiet = true;
        device.accepting = false;
        device.eof = true;
    }
    if (device.accepting && status == CI1302_AUDIO_RUNNING && !device.rx_error) {
        if (!valid)
            device.rx_error = ESP_ERR_INVALID_SIZE;
        else if (xQueueSend(device.frames, &frame, 0) != pdTRUE)
            device.rx_error = ESP_ERR_NO_MEM;
        else {
            ++device.accepted_frames;
            device.last_frame_us = esp_timer_get_time();
        }
    }
    portEXIT_CRITICAL(&device.lock);
}
esp_err_t yoke_voice_device_init(const yoke_voice_device_cfg_t *cfg, yoke_voice_device_handle_t *out) {
    if (!cfg || !out || cfg->uart_num < 0 || cfg->uart_num >= UART_NUM_MAX || !cfg->baud_rate || !cfg->startup_timeout_ms ||
        !GPIO_IS_VALID_OUTPUT_GPIO(cfg->tx_pin) || !GPIO_IS_VALID_GPIO(cfg->rx_pin) || cfg->tx_pin == cfg->rx_pin)
        return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (device.ready) {
        if (cfg->uart_num != device.cfg.uart_num || cfg->tx_pin != device.cfg.tx_pin ||
            cfg->rx_pin != device.cfg.rx_pin || cfg->baud_rate != device.cfg.baud_rate)
            return ESP_ERR_INVALID_STATE;
        *out = &device;
        return ESP_OK;
    }
    /* CI1302 自身没有 deinit；初始化失败后也不重复创建其底层任务。 */
    if (!device.frames) {
        device.frames = xQueueCreate(50, sizeof(yoke_voice_record_frame_t));
        if (!device.frames)
            return ESP_ERR_NO_MEM;
        device.cfg = *cfg;
        ci1302_init(cfg->uart_num, cfg->tx_pin, cfg->rx_pin, -1, -1, cfg->baud_rate);
        ci1302_set_audio_recv_callback(receive);
    } else if (cfg->uart_num != device.cfg.uart_num || cfg->tx_pin != device.cfg.tx_pin ||
               cfg->rx_pin != device.cfg.rx_pin || cfg->baud_rate != device.cfg.baud_rate)
        return ESP_ERR_INVALID_STATE;
    if (!ci1302_wait_startup(cfg->startup_timeout_ms))
        return ESP_ERR_TIMEOUT;
    device.ready = true;
    *out = &device;
    return ESP_OK;
}
bool yoke_voice_device_quiet(yoke_voice_device_handle_t dev) {
    if (!dev)
        return false;
    portENTER_CRITICAL(&dev->lock);
    bool quiet = dev->quiet && esp_timer_get_time() - dev->last_rx_us >= 200000;
    portEXIT_CRITICAL(&dev->lock);
    return quiet;
}
esp_err_t yoke_voice_device_wait_quiet(yoke_voice_device_handle_t dev, unsigned timeout_ms) {
    int64_t end = esp_timer_get_time() + timeout_ms * 1000LL;
    while (!yoke_voice_device_quiet(dev)) {
        if (esp_timer_get_time() >= end)
            return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_OK;
}
esp_err_t yoke_voice_device_volume(yoke_voice_device_handle_t dev, unsigned percent) {
    if (!dev || !dev->ready)
        return ESP_ERR_INVALID_STATE;
    if (percent < 1 || percent > 100)
        return ESP_ERR_INVALID_ARG;
    ci1302_set_volume(1 + percent * 6 / 100);
    return ESP_OK;
}
