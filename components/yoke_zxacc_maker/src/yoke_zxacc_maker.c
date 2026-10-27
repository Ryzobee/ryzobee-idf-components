/** @file yoke_zxacc_maker.c */
#include "yoke_zxacc_maker.h"
#include "rootmaker_utils.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#define TAG "yoke_zxacc_maker"

#define ZXACC_REG_FIRMWARE_VERSION 0x00
#define ZXACC_REG_CHARGE_STATUS 0x02
#define ZXACC_REG_VOLTAGE 0x03
#define ZXACC_REG_SHUTDOWN 0x07
#define ZXACC_REG_WAKEUP_TIME 0x08
#define ZXACC_REG_SHUTDOWN_TIME 0x0a
#define ZXACC_REG_TIMER_WAKEUP 0x0c
#define ZXACC_SHUTDOWN_COMMAND 0x01
#define ZXACC_I2C_TIMEOUT_MS 1000

struct yoke_zxacc_maker {
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_dev_handle_t i2c_device;
    rootmaker_port_t port;
    gpio_num_t pins[2];
    bool bus_ref_held;
    bool initialized;
    bool stopping;
    bool address_held;
};

/* Lifecycle calls are serialized by the application. */
static yoke_zxacc_maker_handle_t s_failed_init;

static bool zxacc_is_ready(yoke_zxacc_maker_handle_t board) {
    return board != NULL && board->initialized && !board->stopping && board->i2c_device != NULL;
}

static esp_err_t zxacc_read(yoke_zxacc_maker_handle_t board, uint8_t reg, uint8_t *data, size_t size) {
    if (board != NULL && !zxacc_is_ready(board))
        return ESP_ERR_INVALID_STATE;
    if (board == NULL || data == NULL || size == 0)
        return ESP_ERR_INVALID_ARG;
    return i2c_master_transmit_receive(board->i2c_device, &reg, sizeof(reg), data, size, ZXACC_I2C_TIMEOUT_MS);
}

static esp_err_t zxacc_write(yoke_zxacc_maker_handle_t board, uint8_t reg, const uint8_t *data, size_t size) {
    if (board != NULL && !zxacc_is_ready(board))
        return ESP_ERR_INVALID_STATE;
    if (board == NULL || data == NULL || size == 0 || size > 4)
        return ESP_ERR_INVALID_ARG;

    uint8_t buffer[5] = { reg };
    memcpy(&buffer[1], data, size);
    return i2c_master_transmit(board->i2c_device, buffer, size + 1, ZXACC_I2C_TIMEOUT_MS);
}

static esp_err_t zxacc_read_u16(yoke_zxacc_maker_handle_t board, uint8_t reg, uint16_t *value) {
    if (value == NULL)
        return ESP_ERR_INVALID_ARG;
    uint8_t data[2];
    esp_err_t ret = zxacc_read(board, reg, data, sizeof(data));
    if (ret == ESP_OK)
        *value = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    return ret;
}

static esp_err_t zxacc_write_u16(yoke_zxacc_maker_handle_t board, uint8_t reg, uint16_t value) {
    const uint8_t data[] = { (uint8_t)value, (uint8_t)(value >> 8) };
    return zxacc_write(board, reg, data, sizeof(data));
}

yoke_zxacc_maker_handle_t yoke_zxacc_maker_init(rootmaker_port_t port) {
    if (s_failed_init != NULL) {
        esp_err_t ret = yoke_zxacc_maker_deinit(s_failed_init);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "previous initialization cleanup still failing: %s", esp_err_to_name(ret));
            return NULL;
        }
        s_failed_init = NULL;
    }
    gpio_num_t sda, scl;
    if (rootmaker_get_port_pins(&port, &sda, &scl) != ESP_OK || sda == scl)
        return NULL;
    yoke_zxacc_maker_handle_t board = calloc(1, sizeof(struct yoke_zxacc_maker));
    if (board == NULL) {
        ESP_LOGE(TAG, "no mem");
        return NULL;
    }
    board->pins[0] = port.pins[0];
    board->pins[1] = port.pins[1];
    board->port = (rootmaker_port_t){ .type = port.type, .pin_num = 2, .pins = board->pins };

    esp_err_t ret = rootmaker_new_i2c_bus(&board->port, &board->i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "new i2c bus failed: %s", esp_err_to_name(ret));
        free(board);
        return NULL;
    }
    board->bus_ref_held = true;
    ret = rootmaker_claim_i2c_address(board->i2c_bus, YOKE_ZXACC_MAKER_DEFAULT_I2C_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C address already bound or unavailable: %s", esp_err_to_name(ret));
        goto fail;
    }

    board->address_held = true;
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = YOKE_ZXACC_MAKER_DEFAULT_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    ret = i2c_master_bus_add_device(board->i2c_bus, &device_config, &board->i2c_device);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "add device failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    board->initialized = true;
    return board;

fail:
    ret = yoke_zxacc_maker_deinit(board);
    if (ret != ESP_OK) {
        s_failed_init = board;
        ESP_LOGE(TAG, "initialization cleanup failed: %s; next init will retry cleanup", esp_err_to_name(ret));
    }
    return NULL;
}

esp_err_t yoke_zxacc_maker_deinit(yoke_zxacc_maker_handle_t board) {
    if (board == NULL)
        return ESP_ERR_INVALID_ARG;
    board->stopping = true;
    board->initialized = false;
    esp_err_t ret;
    if (board->i2c_device != NULL) {
        ret = i2c_master_bus_rm_device(board->i2c_device);
        if (ret != ESP_OK)
            return ret;
        board->i2c_device = NULL;
    }
    if (board->address_held) {
        ret = rootmaker_release_i2c_address(board->i2c_bus, YOKE_ZXACC_MAKER_DEFAULT_I2C_ADDR);
        if (ret != ESP_OK)
            return ret;
        board->address_held = false;
    }
    if (board->bus_ref_held) {
        ret = rootmaker_del_i2c_bus(&board->port);
        if (ret != ESP_OK)
            return ret;
        board->bus_ref_held = false;
    }
    free(board);
    return ESP_OK;
}

esp_err_t yoke_zxacc_maker_get_firmware_version(yoke_zxacc_maker_handle_t board, uint16_t *version) {
    return zxacc_read_u16(board, ZXACC_REG_FIRMWARE_VERSION, version);
}

esp_err_t yoke_zxacc_maker_get_battery_voltage(yoke_zxacc_maker_handle_t board, uint32_t *voltage_mv) {
    if (voltage_mv == NULL)
        return ESP_ERR_INVALID_ARG;
    uint8_t data[4];
    esp_err_t ret = zxacc_read(board, ZXACC_REG_VOLTAGE, data, sizeof(data));
    if (ret == ESP_OK) {
        *voltage_mv = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    }
    return ret;
}

esp_err_t yoke_zxacc_maker_get_charge_state(yoke_zxacc_maker_handle_t board, yoke_zxacc_maker_charge_state_t *state) {
    if (state == NULL)
        return ESP_ERR_INVALID_ARG;
    uint8_t raw;
    esp_err_t ret = zxacc_read(board, ZXACC_REG_CHARGE_STATUS, &raw, sizeof(raw));
    if (ret == ESP_OK) {
        *state = (raw <= YOKE_ZXACC_MAKER_CHARGE_COMPLETE) ? (yoke_zxacc_maker_charge_state_t)raw : YOKE_ZXACC_MAKER_CHARGE_UNKNOWN;
    }
    return ret;
}

esp_err_t yoke_zxacc_maker_get_wakeup_time(yoke_zxacc_maker_handle_t board, uint16_t *time_ms) {
    return zxacc_read_u16(board, ZXACC_REG_WAKEUP_TIME, time_ms);
}

esp_err_t yoke_zxacc_maker_set_wakeup_time(yoke_zxacc_maker_handle_t board, uint16_t time_ms) {
    return zxacc_write_u16(board, ZXACC_REG_WAKEUP_TIME, time_ms);
}

esp_err_t yoke_zxacc_maker_get_shutdown_time(yoke_zxacc_maker_handle_t board, uint16_t *time_ms) {
    return zxacc_read_u16(board, ZXACC_REG_SHUTDOWN_TIME, time_ms);
}

esp_err_t yoke_zxacc_maker_set_shutdown_time(yoke_zxacc_maker_handle_t board, uint16_t time_ms) {
    return zxacc_write_u16(board, ZXACC_REG_SHUTDOWN_TIME, time_ms);
}

esp_err_t yoke_zxacc_maker_set_timer_wakeup(yoke_zxacc_maker_handle_t board, uint32_t seconds) {
    const uint8_t data[] = { (uint8_t)seconds, (uint8_t)(seconds >> 8), (uint8_t)(seconds >> 16), (uint8_t)(seconds >> 24) };
    return zxacc_write(board, ZXACC_REG_TIMER_WAKEUP, data, sizeof(data));
}

esp_err_t yoke_zxacc_maker_shutdown(yoke_zxacc_maker_handle_t board) {
    const uint8_t command = ZXACC_SHUTDOWN_COMMAND;
    return zxacc_write(board, ZXACC_REG_SHUTDOWN, &command, sizeof(command));
}
