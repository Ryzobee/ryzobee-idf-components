#pragma once

#include "esp_err.h"
#include "lvgl.h"
#include "rootmaker_utils.h"

/**
 * @file yoke_screen.h
 * @brief Display and touch initialization for the RootMaker Yoke board.
 *
 * This driver configures the board's built-in 240x240 ST7789 display,
 * CST816T touch controller, and the ESP LVGL port. Board pins and panel
 * orientation are fixed by the hardware design. All application drawing goes
 * through LVGL widgets, images, or canvases; no direct panel drawing API is exposed.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the LCD, touch controller, and LVGL display port.
 *
 * This function owns the board display resources and must be called once
 * before creating LVGL objects. It creates the display's LVGL instance and
 * registers the CST816T touch input device. The component reuses or creates
 * the matching system I2C bus through rootmaker_utils.
 *
 * LCD signals, backlight, touch reset and interrupt are claimed exclusively
 * before hardware setup. Only touch SDA/SCL may be shared with other devices.
 * Lifecycle calls must be serialized with all RootMaker lifecycle operations.
 * Do not initialize/deinitialize while holding the LVGL port lock or from
 * LVGL callbacks.
 *
 * @param[in] port Touch I2C port; pins[0]=SDA, pins[1]=SCL.
 * @return A valid LVGL display handle on success, or NULL if any display,
 * touch, SPI, I2C, or LVGL initialization step fails. If rollback fails,
 * the log requests a yoke_screen_deinit() retry before reinitializing.
 *
 * @note The returned handle is owned by this component. Do not delete it.
 * @note Call yoke_screen_deinit() after stopping application LVGL activity if the
 * display must be shut down.
 */
lv_display_t *yoke_screen_with_lvgl_init(rootmaker_port_t port);

/**
 * @brief Remove the LVGL display and touch devices and release screen hardware.
 *
 * The caller must stop creating or updating LVGL objects before calling this
 * function. The display handle returned by yoke_screen_with_lvgl_init() becomes
 * invalid after this call. The shared system I2C bus is released, but it is
 * deleted only when its final reference is released.
 *
 * @note On failure retry deinit; do not render or initialize until cleanup succeeds.
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the screen is not
 * initialized, or the first cleanup error.
 */
esp_err_t yoke_screen_deinit(void);

#ifdef __cplusplus
}
#endif
