/*
 * Yoke-KEYW demo: one button plus a GPIO indicator LED. Single click toggles
 * the LED; long press reports a hold event.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "rootmaker_utils.h"
#include "yoke_keyw.h"

static const char *TAG = "keyw_demo";

static yoke_keyw_handle_t s_key;

static void on_single_click(button_event_t event, void *user_data) {
    (void)event;
    (void)user_data;
    static bool led_on = false;
    led_on = !led_on;
    ESP_ERROR_CHECK(yoke_keyw_set_led(s_key, led_on));
    ESP_LOGI(TAG, "single click: LED %s", led_on ? "on" : "off");
}

static void on_long_press_start(button_event_t event, void *user_data) {
    (void)event;
    (void)user_data;
    ESP_LOGI(TAG, "long press started");
}

void app_main(void) {
    s_key = yoke_keyw_init(rootmaker_rs_port1);
    ESP_ERROR_CHECK(s_key != NULL ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(yoke_keyw_register_event_callback(s_key, BUTTON_SINGLE_CLICK, on_single_click, NULL));
    ESP_ERROR_CHECK(yoke_keyw_register_event_callback(s_key, BUTTON_LONG_PRESS_START, on_long_press_start, NULL));

    ESP_LOGI(TAG,
             "KEYW ready on RS_PORT1: button=GPIO%d LED=GPIO%d. "
             "Single click toggles the LED, long press to log.",
             rootmaker_rs_port1.pins[0], rootmaker_rs_port1.pins[1]);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
