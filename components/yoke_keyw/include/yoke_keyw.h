/**
 * @file yoke_keyw.h
 * @brief Yoke-KEYW button and GPIO indicator LED driver.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/gpio.h"
#include "iot_button.h"
#include "rootmaker_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback for one subscribed button event.
 *
 * @param[in] event Dispatched button event.
 * @param[in] user_data Context provided when registering the callback.
 *
 * @note Runs on the ESP timer task. Keep the callback short and non-blocking.
 * Only set_led() may be called on this instance from its callback; post other
 * work to an application task. Do not configure/create/delete KEYW instances
 * from any ESP timer callback.
 */
typedef void (*yoke_keyw_event_cb_t)(button_event_t event, void *user_data);

/**
 * @brief Opaque driver instance. Created by yoke_keyw_init(); NULL on failure.
 */
typedef struct yoke_keyw *yoke_keyw_handle_t;

/**
 * @brief Initialize the KEYW module on a RootMaker port.
 *
 * pins[0] is the active-high button and pins[1] is the indicator LED (initially off). Long-press threshold defaults to 1500 ms; change it with
 * yoke_keyw_set_long_press_time(). Returns handle, or NULL on failure.
 *
 * @param[in] port RootMaker port descriptor.
 *
 * @return Instance handle on success; NULL if initialization fails.
 *
 * @note If initialization rollback fails, the unfinished instance is retained
 * internally. The next initialization retries cleanup first and returns
 * NULL while cleanup still fails. Serialize all RootMaker lifecycle calls.
 */
yoke_keyw_handle_t yoke_keyw_init(rootmaker_port_t port);

/**
 * @brief Stop and release this instance.
 *
 * @param[in] keyw KEYW instance handle.
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
 * @note Do not call from button or ESP timer callbacks.
 */
esp_err_t yoke_keyw_deinit(yoke_keyw_handle_t keyw);

/**
 * @brief Change the long-press threshold in milliseconds.
 *
 * Applies immediately to an initialized driver.
 *
 * @param[in] keyw KEYW instance handle.
 * @param[in] time_ms Nonzero duration in milliseconds; must exceed the button
 * component's configured short-press threshold when long-press events are used.
 *
 * @note Recreates the button and resets ongoing gesture recognition.
 * @note Do not call from button or ESP timer callbacks.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_keyw_set_long_press_time(yoke_keyw_handle_t keyw, uint16_t time_ms);

/**
 * @brief Subscribe to one button event (one callback per event).
 *
 * Registration is synchronized with button scanning and may block.
 * Do not call from button/ESP timer callbacks. MULTIPLE_CLICK is unsupported
 * because this simplified API has no click-count argument.
 *
 * Without a DOUBLE_CLICK subscription, SINGLE_CLICK is dispatched immediately on each short release, so a physical double-click invokes it twice. Registering
 * DOUBLE_CLICK enables delayed single/double classification using the button component's click window.
 *
 * @param[in] keyw KEYW instance handle.
 * @param[in] event Button event to subscribe to or unsubscribe from.
 * @param[in] callback Event callback; must not be NULL.
 * @param[in] user_data Application context passed to the callback; may be NULL.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_keyw_register_event_callback(yoke_keyw_handle_t keyw, button_event_t event, yoke_keyw_event_cb_t callback, void *user_data);

/**
 * @brief Unsubscribe from one button event.
 *
 * Synchronized with scanning; after success the removed callback is no longer
 * running. Do not call from button/ESP timer callbacks.
 *
 * @param[in] keyw KEYW instance handle.
 * @param[in] event Button event to subscribe to or unsubscribe from.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - ESP_ERR_NOT_FOUND: No callback is registered for this event.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_keyw_unregister_event_callback(yoke_keyw_handle_t keyw, button_event_t event);

/**
 * @brief Set the indicator LED state: true drives the LED GPIO high, false drives it low.
 *
 * @param[in] keyw KEYW instance handle.
 * @param[in] on True to turn the indicator on; false to turn it off.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_keyw_set_led(yoke_keyw_handle_t keyw, bool on);

#ifdef __cplusplus
}
#endif
