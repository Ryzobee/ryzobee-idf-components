/** @file yoke_rad60.c */
#include "yoke_rad60.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "yoke_rad60_protocol.h"

static const uint8_t RAD60_CMD_GET_RADAR_STATE = 0xD0;
static const uint8_t RAD60_REPORT_MULTI_TARGET = 0x0A;

struct yoke_rad60 {
    SemaphoreHandle_t mutex;
    TaskHandle_t task;
    TickType_t init_tick;
    rootmaker_port_t port;
    gpio_num_t pins[2];
    yoke_rad60_radar_data_t data;
    yoke_rad60_protocol_handle_t protocol;
    SemaphoreHandle_t stopped;
    atomic_bool stopping;
};

/* Lifecycle calls are serialized by the application. */
static yoke_rad60_handle_t s_failed_init;

static void yoke_rad60_task(void *arg);

static void yoke_rad60_process_multitarget_det_info(yoke_rad60_handle_t radar, uint8_t len, uint8_t *buffer) {
    (void)len;
    if (xSemaphoreTake(radar->mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }

    radar->data.radar_info.obj_num = buffer[0];
    if (radar->data.radar_info.obj_num > 2) {
        radar->data.radar_info.obj_num = 2;
    }

    for (uint8_t i = 0; i < radar->data.radar_info.obj_num; i++) {
        uint8_t data_index = 4 + (i * 4);
        radar->data.radar_info.obj[i].range_val = (buffer[data_index] < 32) ? (buffer[data_index] * 6.25) : (buffer[data_index] * 6.85);
        radar->data.radar_info.obj[i].angle_val = (int8_t)buffer[data_index + 1];
        radar->data.radar_info.obj[i].velo_val = (int8_t)(buffer[data_index + 2] / 32);
        radar->data.radar_info.obj[i].objid = buffer[data_index + 3];
    }
    radar->data.is_detected = (radar->data.radar_info.obj_num > 0);
    xSemaphoreGive(radar->mutex);
}

static esp_err_t yoke_rad60_process_report_frame(yoke_rad60_handle_t radar, uint8_t len, uint8_t type, uint8_t *buffer) {
    if (buffer == NULL || len < 5 || (uint16_t)(len - 1) < 4U + (uint16_t)(buffer[0] > 2 ? 2 : buffer[0]) * 4U)
        return ESP_ERR_INVALID_SIZE;
    if (type != RAD60_REPORT_MULTI_TARGET) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    yoke_rad60_process_multitarget_det_info(radar, len - 1, buffer);
    return ESP_OK;
}

static void yoke_rad60_task(void *arg) {
    yoke_rad60_handle_t radar = arg;
    uart_frame_t uart_frame;
    TickType_t last_data_tick = xTaskGetTickCount();
    TickType_t last_query_tick = 0;
    bool query_sent = false;
    vTaskDelay(pdMS_TO_TICKS(100));

    while (!atomic_load(&radar->stopping)) {
        /* Bound each batch so continuous reports cannot postpone health checks. */
        for (unsigned received = 0; received < UART_QUEUE_LENGTH && !atomic_load(&radar->stopping) &&
                                    yoke_rad60_protocol_recv_frame(radar->protocol, &uart_frame, received == 0 ? 50 : 0);
             ++received) {
            bool valid_data_received = false;
            if (uart_frame.frame_type == FRAME_TYPE_REPORT) {
                if (yoke_rad60_process_report_frame(radar, uart_frame.report.len, uart_frame.report.type, uart_frame.report.buffer) == ESP_OK) {
                    valid_data_received = true;
                }
            } else if (uart_frame.frame_type == FRAME_TYPE_RESPONSE && uart_frame.response.cmd == RAD60_CMD_GET_RADAR_STATE) {
                valid_data_received = true;
            }
            yoke_rad60_protocol_free_frame_buffer(&uart_frame);
            if (valid_data_received) {
                last_data_tick = xTaskGetTickCount();
                query_sent = false;
                if (xSemaphoreTake(radar->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    radar->data.is_comm_ok = true;
                    xSemaphoreGive(radar->mutex);
                }
            }
        }

        TickType_t current_tick = xTaskGetTickCount();
        if (current_tick - last_data_tick > pdMS_TO_TICKS(500)) {
            if (xSemaphoreTake(radar->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                radar->data.is_detected = 0;
                radar->data.radar_info.obj_num = 0;
                xSemaphoreGive(radar->mutex);
            }

            if (!query_sent && current_tick - last_query_tick > pdMS_TO_TICKS(600)) {
                yoke_rad60_protocol_write_bytes(radar->protocol, RAD60_CMD_GET_RADAR_STATE, NULL, 0);
                last_query_tick = current_tick;
                query_sent = true;
            }

            if (query_sent && current_tick - last_query_tick > pdMS_TO_TICKS(1500) && current_tick - radar->init_tick > pdMS_TO_TICKS(5000)) {
                if (xSemaphoreTake(radar->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                    radar->data.is_comm_ok = false;
                    xSemaphoreGive(radar->mutex);
                }
                query_sent = false;
            }

            last_data_tick = current_tick;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
    xSemaphoreGive(radar->stopped);
    vTaskSuspend(NULL);
}

yoke_rad60_handle_t yoke_rad60_init(rootmaker_port_t port, uart_port_t uart_num) {
    if (s_failed_init != NULL) {
        esp_err_t ret = yoke_rad60_deinit(s_failed_init);
        if (ret != ESP_OK) {
            ESP_LOGE("yoke_rad60", "previous initialization cleanup still failing: %s", esp_err_to_name(ret));
            return NULL;
        }
        s_failed_init = NULL;
    }
    gpio_num_t tx, rx;
    if (uart_num < UART_NUM_0 || uart_num >= UART_NUM_MAX || rootmaker_get_port_pins(&port, &tx, &rx) != ESP_OK || tx == rx ||
        uart_is_driver_installed(uart_num))
        return NULL;
    yoke_rad60_handle_t radar = calloc(1, sizeof(*radar));
    if (radar == NULL)
        return NULL;
    radar->pins[0] = tx;
    radar->pins[1] = rx;
    radar->port = (rootmaker_port_t){ .type = port.type, .pin_num = 2, .pins = radar->pins };
    atomic_init(&radar->stopping, false);
    esp_err_t ret = rootmaker_use_port(&radar->port);
    if (ret != ESP_OK) {
        free(radar);
        return NULL;
    }
    radar->mutex = xSemaphoreCreateMutex();
    radar->stopped = xSemaphoreCreateBinary();
    if (radar->mutex == NULL || radar->stopped == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }
    radar->init_tick = xTaskGetTickCount();
    ret = yoke_rad60_protocol_init(uart_num, tx, rx, &radar->protocol);
    if (ret != ESP_OK)
        goto fail;
    if (xTaskCreate(yoke_rad60_task, "yoke_rad60", 3 * 1024, radar, 9, &radar->task) != pdPASS) {
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }
    return radar;
fail:
    ESP_LOGE("yoke_rad60", "initialization failed: %s", esp_err_to_name(ret));
    ret = yoke_rad60_deinit(radar);
    if (ret != ESP_OK) {
        s_failed_init = radar;
        ESP_LOGE("yoke_rad60", "initialization cleanup failed: %s; next init will retry cleanup", esp_err_to_name(ret));
    }
    return NULL;
}

esp_err_t yoke_rad60_deinit(yoke_rad60_handle_t radar) {
    if (radar == NULL)
        return ESP_ERR_INVALID_ARG;
    atomic_store(&radar->stopping, true);
    if (radar->task != NULL) {
        xSemaphoreTake(radar->stopped, portMAX_DELAY);
        vTaskDelete(radar->task);
        radar->task = NULL;
    }
    if (radar->protocol != NULL) {
        esp_err_t ret = yoke_rad60_protocol_deinit(radar->protocol);
        if (ret != ESP_OK)
            return ret;
        radar->protocol = NULL;
    }
    esp_err_t ret = rootmaker_release_port(&radar->port);
    if (ret != ESP_OK)
        return ret;
    if (radar->mutex != NULL)
        vSemaphoreDelete(radar->mutex);
    if (radar->stopped != NULL)
        vSemaphoreDelete(radar->stopped);
    free(radar);
    return ESP_OK;
}

esp_err_t yoke_rad60_get_radar_data(yoke_rad60_handle_t radar, yoke_rad60_radar_data_t *data) {
    if (radar == NULL || data == NULL)
        return ESP_ERR_INVALID_ARG;
    if (atomic_load(&radar->stopping))
        return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(radar->mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    *data = radar->data;
    xSemaphoreGive(radar->mutex);
    return data->is_comm_ok ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}
