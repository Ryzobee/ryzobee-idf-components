/** @file yoke_rad60_protocol.c */
#include "yoke_rad60_protocol.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#define TAG "UART_Protocol"
#define YOKE_RAD60_BAUD_RATE 921600U

struct yoke_rad60_protocol {
    uart_port_t uart_num;
    QueueHandle_t uart_queue;
    QueueHandle_t uart_buffer_queue;
    SemaphoreHandle_t uart_semaphore;
    TaskHandle_t uart_frame_task_handle;
    SemaphoreHandle_t stopped;
    atomic_bool stopping;
    uint8_t rx_buf[RX_FRAME_MAX_SIZE + 5];
};

static void uart_frame_task(void *arg);

esp_err_t yoke_rad60_protocol_init(uart_port_t uart_num, gpio_num_t tx_pin, gpio_num_t rx_pin, yoke_rad60_protocol_handle_t *out) {
    if (out == NULL || uart_num < UART_NUM_0 || uart_num >= UART_NUM_MAX || tx_pin == rx_pin) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;
    if (uart_is_driver_installed(uart_num))
        return ESP_ERR_INVALID_STATE;
    yoke_rad60_protocol_handle_t protocol = calloc(1, sizeof(*protocol));
    if (protocol == NULL)
        return ESP_ERR_NO_MEM;
    protocol->uart_num = UART_NUM_MAX;
    atomic_init(&protocol->stopping, false);
    protocol->stopped = xSemaphoreCreateBinary();
    if (protocol->stopped == NULL) {
        free(protocol);
        return ESP_ERR_NO_MEM;
    }
    protocol->uart_semaphore = xSemaphoreCreateMutex();
    if (protocol->uart_semaphore == NULL) {
        vSemaphoreDelete(protocol->stopped);
        free(protocol);
        return ESP_ERR_NO_MEM;
    }
    protocol->uart_buffer_queue = xQueueCreate(UART_QUEUE_LENGTH, sizeof(uart_frame_t));
    if (protocol->uart_buffer_queue == NULL) {
        vSemaphoreDelete(protocol->uart_semaphore);
        vSemaphoreDelete(protocol->stopped);
        free(protocol);
        return ESP_ERR_NO_MEM;
    }

    const uart_config_t uart_config = { .baud_rate = YOKE_RAD60_BAUD_RATE,
                                        .data_bits = UART_DATA_8_BITS,
                                        .parity = UART_PARITY_DISABLE,
                                        .stop_bits = UART_STOP_BITS_1,
                                        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE };
    esp_err_t ret = uart_param_config(uart_num, &uart_config);
    if (ret == ESP_OK) {
        ret = uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (ret == ESP_OK) {
        ret = uart_driver_install(uart_num, RX_BUF_SIZE, TX_BUF_SIZE, UART_QUEUE_LENGTH, &protocol->uart_queue, 0);
    }
    if (ret != ESP_OK) {
        vQueueDelete(protocol->uart_buffer_queue);
        protocol->uart_buffer_queue = NULL;
        vSemaphoreDelete(protocol->uart_semaphore);
        protocol->uart_semaphore = NULL;
        vSemaphoreDelete(protocol->stopped);
        free(protocol);
        return ret;
    }

    protocol->uart_num = uart_num;
    ret = uart_set_rx_timeout(protocol->uart_num, 50);
    if (ret == ESP_OK) {
        ret = uart_set_rx_full_threshold(protocol->uart_num, 50);
    }
    if (ret == ESP_OK && xTaskCreate(uart_frame_task, "uart_queue_task", 3 * 1024, protocol, 10, &protocol->uart_frame_task_handle) != pdPASS) {
        ret = ESP_ERR_NO_MEM;
    }
    if (ret != ESP_OK) {
        /* Give the owner a cleanup-only handle if UART removal fails. */
        if (yoke_rad60_protocol_deinit(protocol) != ESP_OK)
            *out = protocol;
        return ret;
    }

    *out = protocol;
    return ESP_OK;
}

esp_err_t yoke_rad60_protocol_deinit(yoke_rad60_protocol_handle_t protocol) {
    atomic_store(&protocol->stopping, true);
    if (protocol->uart_frame_task_handle != NULL) {
        xSemaphoreTake(protocol->stopped, portMAX_DELAY);
        vTaskDelete(protocol->uart_frame_task_handle);
        protocol->uart_frame_task_handle = NULL;
    }

    if (protocol->uart_buffer_queue != NULL) {
        uart_frame_t frame;
        while (xQueueReceive(protocol->uart_buffer_queue, &frame, 0) == pdTRUE) {
            yoke_rad60_protocol_free_frame_buffer(&frame);
        }
        vQueueDelete(protocol->uart_buffer_queue);
        protocol->uart_buffer_queue = NULL;
    }

    if (protocol->uart_semaphore != NULL) {
        vSemaphoreDelete(protocol->uart_semaphore);
        protocol->uart_semaphore = NULL;
    }

    if (protocol->uart_num != UART_NUM_MAX) {
        esp_err_t ret = uart_driver_delete(protocol->uart_num);
        if (ret != ESP_OK)
            return ret;
        protocol->uart_queue = NULL;
        protocol->uart_num = UART_NUM_MAX;
    }
    vSemaphoreDelete(protocol->stopped);
    free(protocol);
    return ESP_OK;
}

void yoke_rad60_protocol_write_bytes(yoke_rad60_protocol_handle_t protocol, uint16_t cmd, const uint8_t *frame, uint32_t len) {
    if (protocol->uart_semaphore == NULL || protocol->uart_num == UART_NUM_MAX) {
        return;
    }
    if (xSemaphoreTake(protocol->uart_semaphore, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }

    uint32_t total_len = 1 + 1 + 1 + len + 2;
    uint8_t *out_buf = (uint8_t *)malloc(total_len);
    if (out_buf == NULL) {
        xSemaphoreGive(protocol->uart_semaphore);
        return;
    }

    out_buf[0] = PACK_SEND_HEAD;
    out_buf[1] = cmd & 0xFF;
    out_buf[2] = len & 0xFF;
    if (len > 0 && frame != NULL) {
        memcpy(&out_buf[3], frame, len);
    }

    uint16_t check_sum = 0;
    for (uint32_t i = 0; i < 3 + len; i++) {
        check_sum += out_buf[i];
    }
    out_buf[3 + len] = check_sum & 0xFF;
    out_buf[3 + len + 1] = (check_sum >> 8) & 0xFF;

    (void)uart_write_bytes(protocol->uart_num, out_buf, total_len);
    free(out_buf);
    xSemaphoreGive(protocol->uart_semaphore);
}

int yoke_rad60_protocol_recv_frame(yoke_rad60_protocol_handle_t protocol, uart_frame_t *frame, uint32_t timeout_ms) {
    if (frame == NULL || protocol->uart_buffer_queue == NULL) {
        return 0;
    }
    if (timeout_ms != portMAX_DELAY) {
        timeout_ms = pdMS_TO_TICKS(timeout_ms);
    }
    return xQueueReceive(protocol->uart_buffer_queue, frame, timeout_ms);
}

void yoke_rad60_protocol_free_frame_buffer(uart_frame_t *frame) {
    if (frame == NULL) {
        return;
    }
    if (frame->frame_type == FRAME_TYPE_RESPONSE) {
        free(frame->response.buffer);
        frame->response.buffer = NULL;
    } else if (frame->frame_type == FRAME_TYPE_REPORT) {
        free(frame->report.buffer);
        frame->report.buffer = NULL;
    }
}

static void frame_recv_report_callback(yoke_rad60_protocol_handle_t protocol, uint8_t frame_head, uint8_t len, uint8_t type, const uint8_t *buf) {
    uart_frame_t frame;
    frame.frame_type = FRAME_TYPE_REPORT;
    frame.report.head = frame_head;
    frame.report.len = len;
    frame.report.type = type;
    uint8_t data_len = (len > 1) ? (len - 1) : 0;
    if (data_len > 0) {
        frame.report.buffer = (uint8_t *)malloc(data_len);
        if (frame.report.buffer == NULL) {
            return;
        }
        memcpy(frame.report.buffer, buf, data_len);
    } else {
        frame.report.buffer = NULL;
    }
    if (xQueueSend(protocol->uart_buffer_queue, &frame, pdMS_TO_TICKS(10)) != pdTRUE) {
        free(frame.report.buffer);
    }
}

static void frame_recv_response_callback(yoke_rad60_protocol_handle_t protocol, uint8_t frame_head, uint8_t cmd, uint8_t len, const uint8_t *buf) {
    uart_frame_t frame = {
        .frame_type = FRAME_TYPE_RESPONSE,
        .response = {
            .head = frame_head,
            .cmd = cmd,
            .len = len,
            .buffer = NULL,
        },
    };
    if (len > 0) {
        frame.response.buffer = malloc(len);
        if (frame.response.buffer == NULL) {
            return;
        }
        memcpy(frame.response.buffer, buf, len);
    }
    if (xQueueSend(protocol->uart_buffer_queue, &frame, pdMS_TO_TICKS(10)) != pdTRUE) {
        free(frame.response.buffer);
    }
}

static int32_t deal_uart_buffer(yoke_rad60_protocol_handle_t protocol, uint8_t *buffer, uint32_t data_length) {
    uint8_t *deal_ptr = buffer;
    uint32_t total_processed = 0;
    while (data_length > 0) {
        while (data_length > 0 && deal_ptr[0] != PACK_RECV_HEAD && deal_ptr[0] != PACK_REPORT_HEAD) {
            deal_ptr++;
            data_length--;
            total_processed++;
        }
        /* The report length is in byte 1; a response needs byte 2 as well. */
        if (data_length < 2U) {
            break;
        }
        uint32_t packet_len;
        bool checksum_valid;
        if (deal_ptr[0] == PACK_REPORT_HEAD) {
            uint8_t len = deal_ptr[1];
            packet_len = 1 + 1 + len + 1;
            if (len < 1 || packet_len > RX_FRAME_MAX_SIZE) {
                /* Invalid header: advance and search again. Waiting here would
                 * retain the same bad header and hide subsequent valid frames. */
                deal_ptr++;
                data_length--;
                total_processed++;
                continue;
            }
            if (data_length < packet_len) {
                break;
            }
            uint8_t checksum = 0;
            for (uint32_t i = 0; i < 2U + len; i++) {
                checksum += deal_ptr[i];
            }
            checksum_valid = checksum == deal_ptr[2 + len];
            if (checksum_valid) {
                frame_recv_report_callback(protocol, deal_ptr[0], len, deal_ptr[2], &deal_ptr[3]);
            }
        } else {
            if (data_length < 3U) {
                break;
            }
            uint8_t len = deal_ptr[2];
            packet_len = 1 + 1 + 1 + len + 2;
            if (packet_len > RX_FRAME_MAX_SIZE) {
                deal_ptr++;
                data_length--;
                total_processed++;
                continue;
            }
            if (data_length < packet_len) {
                break;
            }
            uint16_t checksum = 0;
            for (uint32_t i = 0; i < 3U + len; i++) {
                checksum += deal_ptr[i];
            }
            uint16_t received_checksum = (uint16_t)deal_ptr[3 + len] | ((uint16_t)deal_ptr[4 + len] << 8);
            checksum_valid = checksum == received_checksum;
            if (checksum_valid) {
                frame_recv_response_callback(protocol, deal_ptr[0], deal_ptr[1], len, &deal_ptr[3]);
            }
        }
        if (!checksum_valid) {
            deal_ptr++;
            data_length--;
            total_processed++;
            continue;
        }
        deal_ptr += packet_len;
        data_length -= packet_len;
        total_processed += packet_len;
    }
    return (int32_t)total_processed;
}

static void uart_frame_task(void *arg) {
    yoke_rad60_protocol_handle_t protocol = arg;
    uart_event_t xEvent;
    uint8_t *rx_buf = protocol->rx_buf;
    uint32_t buffer_total_len = 0;
    while (!atomic_load(&protocol->stopping)) {
        if (xQueueReceive(protocol->uart_queue, (void *)&xEvent, pdMS_TO_TICKS(50)) != pdTRUE) {
            continue;
        }
        if (xEvent.type != UART_DATA) {
            continue;
        }

        size_t event_read_size = 0;
        uart_get_buffered_data_len(protocol->uart_num, &event_read_size);
        if (event_read_size == 0) {
            continue;
        }

        if (buffer_total_len + event_read_size > RX_FRAME_MAX_SIZE) {
            buffer_total_len = 0;
        }

        if (event_read_size > RX_FRAME_MAX_SIZE - buffer_total_len)
            event_read_size = RX_FRAME_MAX_SIZE - buffer_total_len;
        int received = uart_read_bytes(protocol->uart_num, rx_buf + buffer_total_len, event_read_size, pdMS_TO_TICKS(50));
        if (received <= 0)
            continue;
        buffer_total_len += received;

        uint32_t used_size = (uint32_t)deal_uart_buffer(protocol, rx_buf, buffer_total_len);
        if (used_size > 0 && used_size <= buffer_total_len) {
            buffer_total_len -= used_size;
            if (buffer_total_len > 0) {
                memmove(rx_buf, rx_buf + used_size, buffer_total_len);
            }
        }
    }
    xSemaphoreGive(protocol->stopped);
    vTaskSuspend(NULL);
}
