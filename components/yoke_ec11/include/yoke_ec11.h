/**
 * @file yoke_ec11.h
 * @brief Yoke-EC11-V10 I2C peripheral driver.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "rootmaker_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

#define YOKE_EC11_DEVICE_ID 0x4543U
#define YOKE_EC11_DEFAULT_I2C_ADDRESS 0x70U
#define YOKE_EC11_DEFAULT_I2C_SPEED_HZ 100000U
#define YOKE_EC11_DEFAULT_TIMEOUT_MS 1000U

/**
 * @brief Default I2C device configuration initializer.
 */
#define YOKE_EC11_CONFIG_DEFAULT() \
    { .device_address = YOKE_EC11_DEFAULT_I2C_ADDRESS, .scl_speed_hz = YOKE_EC11_DEFAULT_I2C_SPEED_HZ, .timeout_ms = YOKE_EC11_DEFAULT_TIMEOUT_MS }

/**
 * @brief Complete initialization configuration.
 */
typedef struct {
    uint8_t device_address; /**< Active 7-bit I2C address. */
    uint32_t scl_speed_hz;  /**< I2C clock frequency for this device. */
    uint32_t timeout_ms;    /**< I2C transaction timeout in milliseconds. */
} yoke_ec11_config_t;

/**
 * @brief RGB brightness values for one WS2812 LED.
 */
typedef struct {
    uint8_t red;   /**< Red brightness, from 0 to 255. */
    uint8_t green; /**< Green brightness, from 0 to 255. */
    uint8_t blue;  /**< Blue brightness, from 0 to 255. */
} yoke_ec11_rgb_t;

/**
 * @brief Decoded EC11 button state. Reading it clears the peripheral press_count.
 */
typedef struct {
    bool pressed;        /**< Current debounced button state; true when pressed. */
    uint8_t press_count; /**< Short presses since the previous read; cleared by the peripheral after reading. */
} yoke_ec11_key_t;

/**
 * @brief Opaque driver instance. Created by yoke_ec11_init(); NULL on failure.
 */
typedef struct yoke_ec11 *yoke_ec11_handle_t;

/**
 * @brief Attach one Yoke-EC11-V10 to a port.
 *
 * The I2C master bus is created or reused through rootmaker_utils using the port's first two pins (SDA/SCL); the I2C peripheral is resolved automatically (I2C0
 * first, then I2C1). This shortcut uses the default address, speed, and timeout; use yoke_ec11_init_with_config() to override them.
 *
 * @param[in] port Port descriptor (pins[0]=SDA, pins[1]=SCL).
 * @return Driver handle, or NULL on failure (the error is logged).
 *
 * @note If initialization rollback fails, the unfinished instance is retained
 * internally. The next initialization retries cleanup first and returns
 * NULL while cleanup still fails. Serialize all RootMaker lifecycle calls.
 */
yoke_ec11_handle_t yoke_ec11_init(rootmaker_port_t port);

/**
 * @brief Attach one Yoke-EC11-V10 using a complete configuration.
 *
 * Use this interface when the peripheral no longer uses the default address, or when a different I2C speed or transaction timeout is required.
 *
 * @param[in] port I2C port; pins[0]=SDA, pins[1]=SCL.
 * @param[in] config Initialization configuration. Its contents are copied.
 * @return Driver handle, or NULL if the configuration is invalid or initialization fails.
 * @note Uses the same rollback retry and lifecycle rules as yoke_ec11_init().
 */
yoke_ec11_handle_t yoke_ec11_init_with_config(rootmaker_port_t port, const yoke_ec11_config_t *config);

/**
 * @brief Stop and release this instance.
 *
 * @param[in] yoke EC11 instance handle.
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
esp_err_t yoke_ec11_deinit(yoke_ec11_handle_t yoke);

/**
 * @brief Check whether the configured slave responds on I2C.
 *
 * @param[in] yoke EC11 instance handle.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_ec11_probe(yoke_ec11_handle_t yoke);

/**
 * @brief Read and return the fixed device ID (expected value: 0x4543).
 *
 * @param[in] yoke EC11 instance handle.
 * @param[out] device_id Receives the device ID.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_ec11_read_device_id(yoke_ec11_handle_t yoke, uint16_t *device_id);

/**
 * @brief Read button state.
 *
 * The peripheral clears the accumulated press_count after reading.
 *
 * @param[in] yoke EC11 instance handle.
 * @param[out] key Receives the decoded button state.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_ec11_read_key(yoke_ec11_handle_t yoke, yoke_ec11_key_t *key);

/**
 * @brief Return the current or most recent button press duration in milliseconds, with 100 ms resolution.
 *
 * @param[in] yoke EC11 instance handle.
 * @param[out] time_ms Receives the duration in milliseconds.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_ec11_read_key_press_time_ms(yoke_ec11_handle_t yoke, uint16_t *time_ms);

/**
 * @brief Read the signed, continuously accumulated encoder count.
 *
 * @param[in] yoke EC11 instance handle.
 * @param[out] count Receives the accumulated encoder count.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_ec11_read_encoder_count(yoke_ec11_handle_t yoke, int16_t *count);

/**
 * @brief Read the signed encoder delta.
 *
 * This operation clears the peripheral delta counter.
 *
 * @param[in] yoke EC11 instance handle.
 * @param[out] diff Receives the encoder delta; reading clears the peripheral counter.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_ec11_read_encoder_diff(yoke_ec11_handle_t yoke, int16_t *diff);

/**
 * @brief Set one WS2812: index 0 is LED1 (farther from the connector), index 1 is LED2 (nearer).
 *
 * @param[in] yoke EC11 instance handle.
 * @param[in] index LED index: 0 for LED1, 1 for LED2.
 * @param[in] color LED RGB color.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_ec11_set_ws2812(yoke_ec11_handle_t yoke, uint8_t index, yoke_ec11_rgb_t color);

/**
 * @brief Set both WS2812 LEDs through I2C writes.
 *
 * @param[in] yoke EC11 instance handle.
 * @param[in] led1 Color for LED1.
 * @param[in] led2 Color for LED2.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_ec11_set_ws2812_all(yoke_ec11_handle_t yoke, yoke_ec11_rgb_t led1, yoke_ec11_rgb_t led2);

/**
 * @brief Read one WS2812 color; index definitions match yoke_ec11_set_ws2812().
 *
 * @param[in] yoke EC11 instance handle.
 * @param[in] index LED index: 0 for LED1, 1 for LED2.
 * @param[out] color LED RGB color.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_ec11_get_ws2812(yoke_ec11_handle_t yoke, uint8_t index, yoke_ec11_rgb_t *color);

/**
 * @brief Read CTRL1; writing 0x5A puts the peripheral into test mode.
 *
 * @param[in] yoke EC11 instance handle.
 * @param[out] value Register value.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_ec11_read_ctrl1(yoke_ec11_handle_t yoke, uint8_t *value);

/**
 * @brief Write CTRL1; writing 0x5A puts the peripheral into test mode.
 *
 * @param[in] yoke EC11 instance handle.
 * @param[in] value Register value.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_ec11_write_ctrl1(yoke_ec11_handle_t yoke, uint8_t value);

/**
 * @brief Ec11 enter test mode.
 *
 * @param[in] yoke EC11 instance handle.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_ec11_enter_test_mode(yoke_ec11_handle_t yoke);

/**
 * @brief Save the low three bits of a new slave address to peripheral flash.
 *
 * According to the device specification, the change takes effect after reset.
 *
 * @param[in] yoke EC11 instance handle.
 * @param[in] address_low_3bits Low three address bits, from 0 to 7.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_ec11_configure_slave_address(yoke_ec11_handle_t yoke, uint8_t address_low_3bits);

/**
 * @brief Update only the host target address, for example after resetting the peripheral.
 *
 * @param[in] yoke EC11 instance handle.
 * @param[in] device_address 7-bit I2C device address.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - Other errors: Returned by the underlying resource operation.
 */
esp_err_t yoke_ec11_set_active_address(yoke_ec11_handle_t yoke, uint8_t device_address);

#ifdef __cplusplus
}
#endif
