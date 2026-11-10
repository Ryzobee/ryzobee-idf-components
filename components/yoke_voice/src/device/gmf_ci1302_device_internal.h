#pragma once
#include "yoke_voice_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
struct ci1302_device {
    yoke_voice_device_cfg_t cfg;
    portMUX_TYPE lock;
    QueueHandle_t frames;
    void *owner;
    bool ready, accepting, eof, quiet;
    int64_t last_rx_us, last_frame_us;
    uint32_t accepted_frames, rx_errors_start;
    esp_err_t rx_error;
};
