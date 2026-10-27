/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modified for RootMaker: RootMaker panel and touch setup, vendor initialization, shared I2C and exclusive GPIO lifecycle.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_panel_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief LCD panel initialization command */
typedef struct {
    int cmd;               /*!< LCD command */
    const void *data;      /*!< Buffer that holds the command specific data */
    size_t data_bytes;     /*!< Size of `data` in memory, in bytes */
    unsigned int delay_ms; /*!< Delay in milliseconds after this command */
} st7789_lcd_init_cmd_t;

/**
 * @brief LCD panel vendor configuration
 *
 * @note This structure needs to be passed to the `vendor_config` field in `esp_lcd_panel_dev_config_t`.
 */
typedef struct {
    const st7789_lcd_init_cmd_t *init_cmds; /*!< Pointer to initialization commands array */
    uint16_t init_cmds_size;                /*!< Number of commands in above array */
} st7789_vendor_config_t;

/**
 * @brief Create LCD panel for model ST7789
 *
 * @param[in] io LCD panel IO handle
 * @param[in] panel_dev_config general panel device configuration
 * @param[out] ret_panel Returned LCD panel handle
 * @return
 *          - ESP_ERR_INVALID_ARG   if parameter is invalid
 *          - ESP_ERR_NO_MEM        if out of memory
 *          - ESP_OK                on success
 */
esp_err_t lcd_new_panel_st7789_spec(const esp_lcd_panel_io_handle_t io, const esp_lcd_panel_dev_config_t *panel_dev_config, esp_lcd_panel_handle_t *ret_panel);

#ifdef __cplusplus
}
#endif
