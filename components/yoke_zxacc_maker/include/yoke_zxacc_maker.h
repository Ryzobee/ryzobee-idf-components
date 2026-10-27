/**
 * @file yoke_zxacc_maker.h
 * @brief Driver for the ZXACC-maker PY32 power-management baseboard.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "rootmaker_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

#define YOKE_ZXACC_MAKER_DEFAULT_I2C_ADDR 0x73

typedef enum {
    YOKE_ZXACC_MAKER_CHARGE_CHARGING = 0x00,
    YOKE_ZXACC_MAKER_CHARGE_NOT_CHARGING = 0x01,
    YOKE_ZXACC_MAKER_CHARGE_COMPLETE = 0x02,
    YOKE_ZXACC_MAKER_CHARGE_UNKNOWN = 0xff,
} yoke_zxacc_maker_charge_state_t;

/**
 * @brief Opaque driver instance. Created by yoke_zxacc_maker_init(); NULL on failure.
 */
typedef struct yoke_zxacc_maker *yoke_zxacc_maker_handle_t;

/**
 * @brief Initialize the power baseboard on the supplied I2C port (address 0x73).
 *
 * pins[0]=SDA, pins[1]=SCL; pass rootmaker_sys_i2c_port for the on-board bus. The bus is created or reused through rootmaker_utils, so it is shared with the
 * touch panel when both use the same port. Returns handle, or NULL on failure.
 *
 * @note Creates the I2C device without probing it; the first successful read
 * confirms that hardware responds. All data operations are synchronous.
 *
 * @param[in] port RootMaker port descriptor.
 *
 * @return Instance handle on success; NULL if initialization fails.
 *
 * @note If initialization rollback fails, the unfinished instance is retained
 * internally. The next initialization retries cleanup first and returns
 * NULL while cleanup still fails. Serialize all RootMaker lifecycle calls.
 */
yoke_zxacc_maker_handle_t yoke_zxacc_maker_init(rootmaker_port_t port);

/**
 * @brief Stop and release this instance.
 *
 * @param[in] board Power baseboard instance handle.
 *
 * @return
 *     - ESP_OK: All instance resources were released.
 *     - ESP_ERR_INVALID_ARG: The handle is NULL.
 *     - Other errors: Cleanup failed; retain the handle and retry.
 *
 * @note Stop application access before cleanup; serialize lifecycle calls.
 * @note ESP_OK invalidates the handle; set the caller's variable to NULL.
 * @note Once cleanup starts, failures retain the handle for cleanup retry only.
 *       Business operations are rejected.
 */
esp_err_t yoke_zxacc_maker_deinit(yoke_zxacc_maker_handle_t board);

/**
 * @brief Read the PY32 firmware version register.
 *
 * @param[in] board Power baseboard instance handle.
 * @param[out] version Receives the firmware version.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_zxacc_maker_get_firmware_version(yoke_zxacc_maker_handle_t board, uint16_t *version);

/**
 * @brief Read the battery voltage in millivolts.
 *
 * @param[in] board Power baseboard instance handle.
 * @param[out] voltage_mv Receives the battery voltage in millivolts.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_zxacc_maker_get_battery_voltage(yoke_zxacc_maker_handle_t board, uint32_t *voltage_mv);

/**
 * @brief Read the charge state.
 *
 * @param[in] board Power baseboard instance handle.
 * @param[out] state Receives the charge state.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_zxacc_maker_get_charge_state(yoke_zxacc_maker_handle_t board, yoke_zxacc_maker_charge_state_t *state);

/**
 * @brief Read the long-press duration for powering the board on, in ms.
 *
 * @param[in] board Power baseboard instance handle.
 * @param[out] time_ms Receives the duration in milliseconds.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_zxacc_maker_get_wakeup_time(yoke_zxacc_maker_handle_t board, uint16_t *time_ms);

/**
 * @brief Set the long-press duration for powering the board on, in ms.
 *
 * @param[in] board Power baseboard instance handle.
 * @param[in] time_ms Duration in milliseconds.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_zxacc_maker_set_wakeup_time(yoke_zxacc_maker_handle_t board, uint16_t time_ms);

/**
 * @brief Read the long-press duration for powering the board off, in ms.
 *
 * @param[in] board Power baseboard instance handle.
 * @param[out] time_ms Receives the duration in milliseconds.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_zxacc_maker_get_shutdown_time(yoke_zxacc_maker_handle_t board, uint16_t *time_ms);

/**
 * @brief Set the long-press duration for powering the board off, in ms.
 *
 * @param[in] board Power baseboard instance handle.
 * @param[in] time_ms Duration in milliseconds.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_zxacc_maker_set_shutdown_time(yoke_zxacc_maker_handle_t board, uint16_t time_ms);

/**
 * @brief Set a timed wake-up.
 *
 * The board enters its low-power state after this write.
 *
 * @param[in] board Power baseboard instance handle.
 * @param[in] seconds Wake-up delay in seconds.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_zxacc_maker_set_timer_wakeup(yoke_zxacc_maker_handle_t board, uint32_t seconds);

/**
 * @brief Request immediate power-off from the PY32 board.
 *
 * @param[in] board Power baseboard instance handle.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_zxacc_maker_shutdown(yoke_zxacc_maker_handle_t board);

#ifdef __cplusplus
}
#endif
