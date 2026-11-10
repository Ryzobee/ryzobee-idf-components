#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "yoke_voice.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef enum { YOKE_VOICE_IDLE, YOKE_VOICE_PLAYING, YOKE_VOICE_RECORDING, YOKE_VOICE_STOPPING } yoke_voice_state_t;
typedef struct {
    yoke_voice_state_t state;
    esp_err_t last_result;
    uint32_t pcm_bytes;
    bool recording_quiescent;
} yoke_voice_status_t;
/* Called once per accepted session, after cleanup. ESP_OK means processing and
 * UART submission completed (not precise audible completion);
 * ESP_ERR_INVALID_STATE means playback was explicitly cancelled. Runs on the
 * controller task; may submit the next session, must return promptly. Path is
 * valid only during this callback. */
typedef void (*yoke_voice_event_cb_t)(bool recording, esp_err_t result, const char *path);
typedef struct {
    yoke_voice_device_handle_t device;
    yoke_voice_event_cb_t callback;
    unsigned muxer_slice_ms;
    /* Optional application preflight; required bytes include PCM and overhead.
     * Runs under the player API mutex; must not call back into player APIs. */
    esp_err_t (*check_space)(void *ctx, size_t required_bytes);
    void *space_ctx;
} yoke_voice_player_config_t;
#define YOKE_VOICE_PLAYER_CONFIG_DEFAULT(dev) { .device = (dev), .muxer_slice_ms = 60000 }
/* Initialize once, from one application task. Configuration is copied. */
esp_err_t yoke_voice_player_init(const yoke_voice_player_config_t *config);
esp_err_t yoke_voice_play_file(const char *path);
/* HTTP/HTTPS MP3; uses an existing network, never connects/reconnects or syncs time.
 * No network: logs a message and returns ESP_ERR_INVALID_STATE synchronously,
 * without accepting a session or invoking the completion callback.
 * Network is rechecked by the worker; later failures use the completion callback.
 */
esp_err_t yoke_voice_play_http(const char *url);
esp_err_t yoke_voice_stop(void);
esp_err_t yoke_voice_volume(unsigned percent);
esp_err_t yoke_voice_record_start(const char *path, unsigned seconds);
/* 文件名前缀不含扩展名，例如 /recordings/mux_0001；实际生成 mux_0001_000.wav 等分片。 */
esp_err_t yoke_voice_record_muxer_start(const char *path, unsigned seconds);
esp_err_t yoke_voice_record_stop(void);
esp_err_t yoke_voice_status(yoke_voice_status_t *status);

#ifdef __cplusplus
}
#endif
