/*
 * Yoke-EC11 demo: print encoder and button changes, and set both LEDs to a
 * fixed color.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "rootmaker_utils.h"

#include "yoke_ec11.h"

static const char *TAG = "ec11_demo";

void app_main(void) {
    ESP_LOGI(TAG, "initializing EC11 on RS_PORT1: SDA=GPIO%d SCL=GPIO%d, address=0x%02X", rootmaker_rs_port1.pins[0], rootmaker_rs_port1.pins[1],
             YOKE_EC11_DEFAULT_I2C_ADDRESS);

    yoke_ec11_handle_t ec11 = yoke_ec11_init(rootmaker_rs_port1);
    if (ec11 == NULL) {
        ESP_LOGE(TAG, "EC11 initialization failed; demo stopped");
        return;
    }

    uint16_t device_id = 0;
    ESP_ERROR_CHECK(yoke_ec11_read_device_id(ec11, &device_id));
    ESP_LOGI(TAG, "EC11 device ID: 0x%04X", device_id);

    const yoke_ec11_rgb_t color = { .red = 0, .green = 32, .blue = 64 };
    ESP_ERROR_CHECK(yoke_ec11_set_ws2812_all(ec11, color, color));
    ESP_LOGI(TAG, "LEDs set to RGB(%u, %u, %u)", color.red, color.green, color.blue);

    bool last_pressed = false;

    while (1) {
        int16_t diff = 0;
        esp_err_t ret = yoke_ec11_read_encoder_diff(ec11, &diff);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "encoder diff read failed: %s", esp_err_to_name(ret));
        } else if (diff != 0) {
            int16_t count = 0;
            ret = yoke_ec11_read_encoder_count(ec11, &count);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "encoder: count=%d, diff=%+d", count, diff);
            } else {
                ESP_LOGE(TAG, "encoder count read failed: %s", esp_err_to_name(ret));
            }
        }

        yoke_ec11_key_t key = { 0 };
        ret = yoke_ec11_read_key(ec11, &key);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "button read failed: %s", esp_err_to_name(ret));
        } else {
            if (key.pressed != last_pressed) {
                ESP_LOGI(TAG, "button: %s", key.pressed ? "pressed" : "released");
                last_pressed = key.pressed;
            }
            if (key.press_count > 0) {
                ESP_LOGI(TAG, "button press count: %u", key.press_count);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
