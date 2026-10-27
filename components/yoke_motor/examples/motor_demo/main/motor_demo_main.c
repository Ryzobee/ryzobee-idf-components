/*
 * Yoke-motor demo: drive a motor forward at a fixed speed on RS_PORT1.
 */

#include "esp_log.h"

#include "rootmaker_utils.h"
#include "yoke_motor.h"

static const char *TAG = "motor_demo";

void app_main(void) {
    yoke_motor_handle_t motor = yoke_motor_init(rootmaker_rs_port1);
    ESP_ERROR_CHECK(motor != NULL ? ESP_OK : ESP_FAIL);

    ESP_LOGI(TAG, "motor initialized on RS_PORT1: PWMA=GPIO%d PWMB=GPIO%d", rootmaker_rs_port1.pins[0], rootmaker_rs_port1.pins[1]);
    ESP_ERROR_CHECK(yoke_motor_set_speed_percent(motor, 100));
    ESP_ERROR_CHECK(yoke_motor_forward(motor));
    ESP_LOGI(TAG, "motor running forward at %d%%", 100);
}
