/**
 * @file yoke_rgbw.h
 * @brief RGBW WS2812 LED strip driver.
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "driver/gpio.h"
#include "rootmaker_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque driver instance. Created by yoke_rgbw_init(); NULL on failure.
 */
typedef struct yoke_rgbw *yoke_rgbw_handle_t;

/**
 * @brief Initialize an RGBW WS2812 strip using GRBW byte order.
 *
 * pins[0] is the data GPIO; led_num must be greater than zero.
 * Only this pin is claimed. Initialization does not transmit an off frame.
 * Serialize all operations on the same strip from application tasks.
 * Each transmission ends with a 300 us low reset interval.
 *
 * @param[in] port RootMaker port descriptor.
 * @param[in] led_num Number of LEDs; must be greater than zero.
 *
 * @return Instance handle on success; NULL if initialization fails.
 *
 * @note If initialization rollback fails, the unfinished instance is retained
 * internally. The next initialization retries cleanup first and returns
 * NULL while cleanup still fails. Serialize all RootMaker lifecycle calls.
 */
yoke_rgbw_handle_t yoke_rgbw_init(rootmaker_port_t port, uint16_t led_num);

/**
 * @brief Stop and release this instance; does not transmit an off frame.
 *
 * @param[in] rgbw RGBW strip instance handle.
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
esp_err_t yoke_rgbw_deinit(yoke_rgbw_handle_t rgbw);

/**
 * @brief Set every LED to the same RGBW color and wait for transmission.
 *
 * @param[in] rgbw RGBW strip instance handle.
 * @param[in] red Red brightness, from 0 to 255.
 * @param[in] green Green brightness, from 0 to 255.
 * @param[in] blue Blue brightness, from 0 to 255.
 * @param[in] white White brightness, from 0 to 255.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: The handle is NULL.
 *     - ESP_ERR_INVALID_STATE: The strip is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_rgbw_set_all(yoke_rgbw_handle_t rgbw, uint8_t red, uint8_t green, uint8_t blue, uint8_t white);

/**
 * @brief Set one LED by zero-based index and wait for the complete strip transmission.
 *
 * @param[in] rgbw RGBW strip instance handle.
 * @param[in] index Zero-based index.
 * @param[in] red Red brightness, from 0 to 255.
 * @param[in] green Green brightness, from 0 to 255.
 * @param[in] blue Blue brightness, from 0 to 255.
 * @param[in] white White brightness, from 0 to 255.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The strip is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_rgbw_set_pixel(yoke_rgbw_handle_t rgbw, uint16_t index, uint8_t red, uint8_t green, uint8_t blue, uint8_t white);

#ifdef __cplusplus
}
#endif
