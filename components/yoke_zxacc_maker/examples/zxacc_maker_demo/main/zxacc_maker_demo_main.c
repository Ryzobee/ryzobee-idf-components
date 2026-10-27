/*
 * ZXACC-maker demo: initialize the power-management baseboard on the board's
 * system I2C (GPIO40/41), read firmware version, battery voltage, charge state, and
 * button power timing, then print them once per second.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "yoke_zxacc_maker.h"

static const char *TAG = "zxacc_maker_demo";

static const char *charge_to_string(yoke_zxacc_maker_charge_state_t state) {
    switch (state) {
        case YOKE_ZXACC_MAKER_CHARGE_CHARGING:
            return "charging";
        case YOKE_ZXACC_MAKER_CHARGE_NOT_CHARGING:
            return "not charging";
        case YOKE_ZXACC_MAKER_CHARGE_COMPLETE:
            return "complete";
        default:
            return "unknown";
    }
}

void app_main(void) {
    yoke_zxacc_maker_handle_t board = yoke_zxacc_maker_init(rootmaker_sys_i2c_port);
    if (board == NULL) {
        ESP_LOGE(TAG, "ZXACC-maker initialization failed");
        return;
    }
    ESP_LOGI(TAG, "ZXACC-maker ready on system I2C SDA=41 SCL=40 addr=0x%02X", YOKE_ZXACC_MAKER_DEFAULT_I2C_ADDR);

    while (1) {
        uint16_t version = 0;
        uint32_t voltage_mv = 0;
        yoke_zxacc_maker_charge_state_t charge = YOKE_ZXACC_MAKER_CHARGE_UNKNOWN;
        uint16_t wake_ms = 0;
        uint16_t shutdown_ms = 0;

        if (yoke_zxacc_maker_get_firmware_version(board, &version) == ESP_OK && yoke_zxacc_maker_get_battery_voltage(board, &voltage_mv) == ESP_OK &&
            yoke_zxacc_maker_get_charge_state(board, &charge) == ESP_OK && yoke_zxacc_maker_get_wakeup_time(board, &wake_ms) == ESP_OK &&
            yoke_zxacc_maker_get_shutdown_time(board, &shutdown_ms) == ESP_OK) {
            ESP_LOGI(TAG, "fw=0x%04X voltage=%umV charge=%s wake=%ums shutdown=%ums", version, voltage_mv, charge_to_string(charge), wake_ms, shutdown_ms);
        } else {
            ESP_LOGE(TAG, "read failed");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
