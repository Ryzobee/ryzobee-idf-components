/*
 * Yoke-RGBW demo: initialize a strip of 4 RGBW LEDs and cycle through
 * red, green, blue, and white, then off.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "rootmaker_utils.h"

#include "yoke_rgbw.h"

#define RGBW_LED_NUM 4

static const char *TAG = "rgbw_demo";

void app_main(void) {
    yoke_rgbw_handle_t rgbw = yoke_rgbw_init(rootmaker_rs_port1, RGBW_LED_NUM);
    if (rgbw == NULL) {
        ESP_LOGE(TAG, "RGBW initialization failed");
        return;
    }

    const struct {
        uint8_t red, green, blue, white;
        const char *name;
    } colors[] = {
        { 255, 0,   0,   0,   "red"   },
        { 0,   255, 0,   0,   "green" },
        { 0,   0,   255, 0,   "blue"  },
        { 0,   0,   0,   255, "white" },
        { 0,   0,   0,   0,   "off"   },
    };

    ESP_LOGI(TAG, "strip on RS_PORT1 pin 1 (GPIO%d), %d LEDs", rootmaker_rs_port1.pins[0], RGBW_LED_NUM);
    while (1) {
        for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i) {
            ESP_ERROR_CHECK(yoke_rgbw_set_all(rgbw, colors[i].red, colors[i].green, colors[i].blue, colors[i].white));
            ESP_LOGI(TAG, "%s", colors[i].name);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
