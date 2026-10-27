/**
 * @file yoke_rad60.h
 * @brief Multi-instance RAD60 UART radar driver.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/uart.h"
#include "rootmaker_utils.h"

#define YOKE_RAD60_DEFAULT_UART_NUM UART_NUM_1

/**
 * @brief One decoded radar target.
 */
typedef struct {
    uint16_t range_val; /**< Distance in cm. */
    int16_t angle_val;  /**< Angle in degrees. */
    int16_t velo_val;   /**< Velocity in cm/s. */
    uint8_t objid;      /**< Target ID. */
    uint8_t reserved0;  /**< Reserved. */
} yoke_rad60_target_t;

/**
 * @brief Latest target detection result, with up to two targets.
 */
typedef struct {
    uint8_t obj_num;               /**< Number of detected targets. */
    uint8_t reserved[3];           /**< Reserved. */
    yoke_rad60_target_t obj[2];    /**< Target data; up to two targets are supported. */
} yoke_rad60_detection_t;

/**
 * @brief Cached detection data and communication status for one instance.
 */
typedef struct {
    bool is_comm_ok;                   /**< True when radar communication is healthy. */
    bool is_detected;                  /**< True when at least one target is detected. */
    yoke_rad60_detection_t radar_info; /**< Radar detection data. */
} yoke_rad60_radar_data_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque, independent radar instance.
 */
typedef struct yoke_rad60 *yoke_rad60_handle_t;

/**
 * @brief Create one radar (pins[0]=TX, pins[1]=RX).
 *
 * NULL on failure. Each instance requires a free UART. Serialize lifecycle calls; stop application reads before deinit.
 *
 * @param[in] port RootMaker port descriptor.
 * @param[in] uart_num UART controller; must be free and different for each radar instance.
 *
 * @return Instance handle on success; NULL if initialization fails.
 *
 * @note If initialization rollback fails, the unfinished instance is retained
 * internally. The next initialization retries cleanup first and returns
 * NULL while cleanup still fails. Serialize all RootMaker lifecycle calls.
 */
yoke_rad60_handle_t yoke_rad60_init(rootmaker_port_t port, uart_port_t uart_num);

/**
 * @brief Stop and release this instance.
 *
 * @param[in] radar Radar instance handle.
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
esp_err_t yoke_rad60_deinit(yoke_rad60_handle_t radar);

/**
 * @brief Copy the latest cached snapshot; does not wait for a new UART frame.
 *
 * The snapshot mutex may block for up to 100 ms. Only obj[0..obj_num-1]
 * contains active targets; obj_num is at most 2.
 *
 * ESP_ERR_NOT_SUPPORTED means communication is unhealthy; the output remains valid.
 *
 * @param[in] radar Radar instance handle.
 * @param[out] data Receives the cached radar snapshot.
 *
 * @return
 *     - ESP_OK: Operation completed successfully.
 *     - ESP_ERR_INVALID_ARG: An argument is invalid.
 *     - ESP_ERR_INVALID_STATE: The instance is unavailable or cleanup has started.
 *     - ESP_ERR_TIMEOUT: The snapshot lock could not be acquired.
 *     - ESP_ERR_NOT_SUPPORTED: Communication is unhealthy; the output snapshot remains valid.
 */
esp_err_t yoke_rad60_get_radar_data(yoke_rad60_handle_t radar, yoke_rad60_radar_data_t *data);

#ifdef __cplusplus
}
#endif
