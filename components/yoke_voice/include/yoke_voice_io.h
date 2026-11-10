#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "yoke_voice.h"
#include "esp_gmf_io.h"
#include "esp_gmf_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CI1302 录音包：40 ms Opus，支持裸 80 字节或 120 字节 Opus + 2 字节 RMS。 */
#define YOKE_VOICE_RECORD_FRAME_MS 40
#define YOKE_VOICE_RECORD_PCM_BYTES (16000 * YOKE_VOICE_RECORD_FRAME_MS / 1000 * 2)
#define YOKE_VOICE_RECORD_MAX_PACKET 120

typedef struct {
    uint16_t size;
    uint8_t data[YOKE_VOICE_RECORD_MAX_PACKET];
} yoke_voice_record_frame_t;

/* 保留完整 Opus 包，去除 RMS；拒绝其他长度，不拆包或拼包。 */
static inline bool yoke_voice_record_frame_parse(const uint8_t *data, size_t size, yoke_voice_record_frame_t *frame) {
    if (!data || !frame || (size != 80 && size != YOKE_VOICE_RECORD_MAX_PACKET + 2))
        return false;
    size_t payload_size = size == 80 ? 80 : YOKE_VOICE_RECORD_MAX_PACKET;
    memset(frame, 0, sizeof(*frame));
    frame->size = payload_size;
    memcpy(frame->data, data, payload_size);
    return true;
}

typedef struct {
    yoke_voice_device_handle_t device;
    esp_gmf_io_dir_t dir;
    unsigned max_chunk, flow_timeout_ms, tx_timeout_ms, rx_timeout_ms;
} yoke_voice_io_cfg_t;
#define YOKE_VOICE_IO_CFG_DEFAULT(dev, direction) {dev, direction, 1024, 1000, 200, 3000}

typedef struct {
    esp_err_t error;
    uint32_t submitted_bytes, accepted_frames;
    bool opened, eof, cancelled;
} yoke_voice_io_status_t;

/* IO 原型及其复制实例均不占用硬件，open 时才取得单设备使用权。 */
esp_gmf_err_t yoke_voice_io_init(const yoke_voice_io_cfg_t *cfg, esp_gmf_io_handle_t *out);
esp_gmf_err_t yoke_voice_io_register(esp_gmf_pool_handle_t pool, yoke_voice_device_handle_t dev);
void yoke_voice_io_cancel(esp_gmf_io_handle_t io);
/* 停止接收新录音包，但允许 GMF 排空已经接收的包。 */
void yoke_voice_io_finish(esp_gmf_io_handle_t io);
esp_err_t yoke_voice_io_status(esp_gmf_io_handle_t io, yoke_voice_io_status_t *out);

#ifdef __cplusplus
}
#endif
