#pragma once
#include "yoke_voice_runtime.h"
#include "freertos/semphr.h"
struct yoke_voice_runtime {
    esp_gmf_pool_handle_t pool;
    yoke_voice_device_handle_t device;
    SemaphoreHandle_t session;
    bool codecs_loaded, muxers_loaded;
};
esp_gmf_err_t player_wav_register(esp_gmf_pool_handle_t pool);
uint32_t player_wav_bytes(esp_gmf_io_handle_t io);
esp_err_t player_wav_error(esp_gmf_io_handle_t io);
