/**
 * @file yoke_motor.h
 * @brief Yoke-motor brushed DC motor driver.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/gpio.h"
#include "rootmaker_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque driver instance. Created by yoke_motor_init(); NULL on failure.
 */
typedef struct yoke_motor *yoke_motor_handle_t;

/**
 * @brief Create and enable the motor on a port (pins[0]=PWMA, pins[1]=PWMB), leaving it coasting.
 *
 * 20 kHz / 10 MHz / automatic MCPWM group / 50% initial speed. Returns handle, or NULL on failure.
 *
 * @param[in] port RootMaker port descriptor.
 *
 * @return Instance handle on success; NULL if initialization fails.
 *
 * @note If initialization rollback fails, the driver retains its resources and
 * retries cleanup before the next initialization. That call returns NULL while
 * the cleanup error persists; it does not create another motor instance.
 */
yoke_motor_handle_t yoke_motor_init(rootmaker_port_t port);

/**
 * @brief Stop and release this instance.
 *
 * @param[in] motor Motor instance handle.
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
esp_err_t yoke_motor_deinit(yoke_motor_handle_t motor);

/**
 * @brief Set the PWM duty from 0 to 100 percent (not closed-loop RPM).
 *
 * Does not change direction or release coast/brake. Call forward() or reverse()
 * to resume after stopping. Serialize operations on the same motor.
 *
 * @param[in] motor Motor instance handle.
 * @param[in] percent Speed, from 0 to 100 percent.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_motor_set_speed_percent(yoke_motor_handle_t motor, uint8_t percent);

/**
 * @brief Drive forward.
 *
 * @param[in] motor Motor instance handle.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: The handle is NULL.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_motor_forward(yoke_motor_handle_t motor);

/**
 * @brief Drive in reverse.
 *
 * @param[in] motor Motor instance handle.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: The handle is NULL.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_motor_reverse(yoke_motor_handle_t motor);

/**
 * @brief Stop by coasting (fast decay).
 *
 * @param[in] motor Motor instance handle.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: The handle is NULL.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_motor_coast(yoke_motor_handle_t motor);

/**
 * @brief Stop by braking (slow decay).
 *
 * @param[in] motor Motor instance handle.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: The handle is NULL.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_motor_brake(yoke_motor_handle_t motor);

#ifdef __cplusplus
}
#endif
