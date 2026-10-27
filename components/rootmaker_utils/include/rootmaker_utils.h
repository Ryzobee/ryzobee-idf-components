#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file rootmaker_utils.h
 * @brief RootMaker interface port model and I2C bus pool.
 *
 * rootmaker_utils provides two things:
 * 1. A `rootmaker_port_t` descriptor that describes one physical interface
 *    (pins) so drivers can be configured by port instead of raw GPIO numbers.
 *    Predefined board ports are provided; applications can also describe
 *    custom ports using their own GPIO arrays.
 * 2. A shared I2C master bus pool keyed by SDA/SCL pins. Matching pins reuse
 *    the existing bus; new buses use an available I2C controller.
 *    GPIO claims and device address reservations prevent resource conflicts.
 */

/**
 * @brief Interface types.
 */
typedef enum {
    ROOTMAKER_PORT_TYPE_RS_BUS = 0, /**< General-purpose port (2 signal pins). */
    ROOTMAKER_PORT_TYPE_S_BUS,      /**< S_BUS group. */
    ROOTMAKER_PORT_TYPE_F_BUS,      /**< F_BUS group. */
} rootmaker_port_type_t;

/**
 * @brief Physical pin descriptor; does not itself allocate hardware.
 *
 * Peripheral drivers copy the signal pins they use during initialization.
 * Direct rootmaker_use_port() callers must keep the pin array alive and
 * unchanged until rootmaker_release_port(). The type labels the connector;
 * exclusivity is determined by overlapping GPIO numbers, regardless of type.
 */
typedef struct {
    rootmaker_port_type_t type;
    uint8_t pin_num;        /**< Number of valid pins. */
    const gpio_num_t *pins; /**< Read-only GPIO array of `pin_num` entries. */
} rootmaker_port_t;

/**
 * @brief Predefined RootMaker board ports (pins come from `rootmaker_pin.h`).
 */
extern const rootmaker_port_t rootmaker_rs_port1;
extern const rootmaker_port_t rootmaker_rs_port2;
extern const rootmaker_port_t rootmaker_rs_port3;
extern const rootmaker_port_t rootmaker_rs_port4;
extern const rootmaker_port_t rootmaker_s_bus;
extern const rootmaker_port_t rootmaker_f_bus;

/**
 * @brief On-board system I2C (GPIOs from rootmaker_pin.h), shared by the power baseboard and the touch panel.
 */
extern const rootmaker_port_t rootmaker_sys_i2c_port;

/**
 * @brief Read the first two pins of a port.
 *
 * @param[in] port  Port descriptor.
 * @param[out] pin_a Out: pins[0].
 * @param[out] pin_b Out: pins[1].
 * @return ESP_OK, or ESP_ERR_INVALID_ARG for missing or invalid GPIOs.
 */
esp_err_t rootmaker_get_port_pins(const rootmaker_port_t *port, gpio_num_t *pin_a, gpio_num_t *pin_b);

/**
 * @brief Read one pin of a port.
 *
 * @param[in] port  Port descriptor.
 * @param[in] index Pin index within the port (0-based).
 * @param[out] pin   Out: pins[index].
 * @return ESP_OK, or ESP_ERR_INVALID_ARG for an invalid index or GPIO.
 */
esp_err_t rootmaker_get_port_pin(const rootmaker_port_t *port, uint8_t index, gpio_num_t *pin);

/**
 * @brief Claim all GPIOs in a port for exclusive use.
 *
 * The claim fails if any GPIO overlaps another exclusive port or an active shared I2C bus. I2C drivers must use rootmaker_new_i2c_bus() instead so SDA/SCL can
 * be shared.
 * Keep the pin array alive and unchanged until release. Serialize all
 * resource claims/releases, including I2C acquisition, across tasks.
 *
 * @param[in] port Port to claim.
 * @return ESP_OK, ESP_ERR_INVALID_STATE if already claimed, ESP_ERR_NO_MEM if the table is full.
 */
esp_err_t rootmaker_use_port(const rootmaker_port_t *port);

/**
 * @brief Release a previously claimed port.
 *
 * ESP_ERR_NOT_FOUND if it was not claimed.
 *
 * @param[in] port RootMaker port descriptor.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_NOT_FOUND: No matching exclusive claim exists.
 */
esp_err_t rootmaker_release_port(const rootmaker_port_t *port);

/**
 * @brief Return true when any GPIO in the port is used exclusively or by a shared I2C bus.
 *
 * @param[in] port RootMaker port descriptor.
 *
 * @return True when the port has an active resource claim; otherwise false.
 */
bool rootmaker_port_is_used(const rootmaker_port_t *port);

/**
 * @brief Get or create an I2C master bus for a port.
 *
 * The first two pins are SDA/SCL. Identical ordered pairs reuse a bus by
 * reference count. Partially overlapping or swapped pairs, and GPIOs held
 * by exclusive claims, return ESP_ERR_INVALID_STATE. SDA must differ from SCL.
 * Disjoint pairs use the first available I2C controller. Externally occupied
 * controllers are skipped; externally created buses are not adopted.
 *
 * @param[in] port    Port descriptor holding the SDA/SCL pins.
 * @param[out] out_bus Out: I2C master bus handle.
 * @note Each successful call must be paired with one rootmaker_del_i2c_bus()
 * call. The underlying bus is deleted after the final release.
 * @note Bus acquisition and release are not thread-safe. Call them serially
 * from the application lifecycle manager.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, or an error from i2c_new_master_bus().
 */
esp_err_t rootmaker_new_i2c_bus(const rootmaker_port_t *port, i2c_master_bus_handle_t *out_bus);

/**
 * @brief Release one reference to an I2C bus previously acquired by rootmaker_new_i2c_bus().
 *
 * The bus is deleted only when the final reference is released. The bus is located by matching the port's SDA/SCL pins, so the caller only passes the same port
 * used at acquisition time.
 *
 * @param[in] port  Port whose pins identify the bus.
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_NOT_FOUND, or an error from i2c_del_master_bus().
 */
esp_err_t rootmaker_del_i2c_bus(const rootmaker_port_t *port);

/**
 * @brief Reserve one bus/address binding.
 *
 * Serial lifecycle calls only; duplicates return ESP_ERR_INVALID_STATE.
 *
 * @param[in] bus I2C master bus handle.
 * @param[in] address 7-bit I2C device address.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The bus/address is already reserved.
 *     - ESP_ERR_NO_MEM: The reservation table is full.
 */
esp_err_t rootmaker_claim_i2c_address(i2c_master_bus_handle_t bus, uint8_t address);

/**
 * @brief Release a binding after removing the device.
 *
 * @param[in] bus I2C master bus handle.
 * @param[in] address 7-bit I2C device address.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_NOT_FOUND: No matching reservation exists.
 */
esp_err_t rootmaker_release_i2c_address(i2c_master_bus_handle_t bus, uint8_t address);

#ifdef __cplusplus
}
#endif
