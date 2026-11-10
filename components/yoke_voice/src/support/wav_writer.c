/* SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include <stdlib.h>
#include <string.h>
#include "yoke_voice_runtime.h"
#include "runtime_internal.h"
#include "esp_gmf_io.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"
typedef struct {
    esp_gmf_io_t base;
    portMUX_TYPE lock;
    FILE *file; // 借用会话文件，GMF IO 不拥有 FILE。
    uint32_t bytes;
    esp_err_t error;
} wav_io_t;
static esp_gmf_err_t open_file(esp_gmf_io_handle_t handle) {
    return ((wav_io_t *)handle)->file ? ESP_GMF_ERR_OK : ESP_GMF_ERR_INVALID_STATE;
}
static esp_gmf_err_t close_file(esp_gmf_io_handle_t handle) {
    (void)handle;
    return ESP_GMF_ERR_OK; // FILE 由播放器回填 WAV 头并关闭。
}
static esp_gmf_err_io_t acquire(esp_gmf_io_handle_t io, void *p, uint32_t n, int ticks) {
    (void)io; (void)p; (void)n; (void)ticks;
    return ESP_GMF_IO_OK;
}
static esp_gmf_err_io_t write_pcm(esp_gmf_io_handle_t handle, void *payload, int ticks) {
    (void)ticks;
    wav_io_t *io = handle;
    esp_gmf_payload_t *p = payload;
    if (!io->file || (p->valid_size & 1) || (p->valid_size && !p->buf) ||
        (p->valid_size && fwrite(p->buf, 1, p->valid_size, io->file) != p->valid_size)) {
        portENTER_CRITICAL(&io->lock);
        io->error = ESP_FAIL;
        portEXIT_CRITICAL(&io->lock);
        return ESP_GMF_IO_FAIL;
    }
    portENTER_CRITICAL(&io->lock);
    io->bytes += p->valid_size;
    portEXIT_CRITICAL(&io->lock);
    return ESP_GMF_IO_OK;
}
static esp_gmf_err_t del(esp_gmf_obj_handle_t obj) {
    esp_gmf_io_deinit(obj);
    free(obj);
    return ESP_GMF_ERR_OK;
}
static esp_gmf_err_t create(void *cfg, esp_gmf_obj_handle_t *out) {
    (void)cfg;
    wav_io_t *io = calloc(1, sizeof(*io));
    if (!io)
        return ESP_GMF_ERR_MEMORY_LACK;
    io->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    io->base.parent.new_obj = create;
    io->base.parent.del_obj = del;
    io->base.dir = ESP_GMF_IO_DIR_WRITER;
    io->base.type = ESP_GMF_IO_TYPE_BYTE;
    io->base.open = open_file;
    io->base.close = close_file;
    io->base.acquire_write = acquire;
    io->base.release_write = write_pcm;
    esp_gmf_io_cfg_t c = {0};
    esp_gmf_err_t ret = esp_gmf_io_init(io, &c);
    if (ret == ESP_GMF_ERR_OK)
        ret = esp_gmf_obj_set_tag(io, "io_wav_pcm");
    if (ret != ESP_GMF_ERR_OK) {
        del(io);
        return ret;
    }
    *out = io;
    return ESP_GMF_ERR_OK;
}
esp_gmf_err_t player_wav_register(esp_gmf_pool_handle_t pool) {
    esp_gmf_obj_handle_t io = NULL;
    esp_gmf_err_t ret = create(NULL, &io);
    if (ret != ESP_GMF_ERR_OK)
        return ret;
    ret = esp_gmf_pool_register_io(pool, io, NULL);
    if (ret != ESP_GMF_ERR_OK)
        esp_gmf_obj_delete(io);
    return ret;
}
static void player_wav_set_file(esp_gmf_io_handle_t io, FILE *file) { ((wav_io_t *)io)->file = file; }
uint32_t player_wav_bytes(esp_gmf_io_handle_t handle) {
    wav_io_t *io = handle;
    portENTER_CRITICAL(&io->lock);
    uint32_t bytes = io->bytes;
    portEXIT_CRITICAL(&io->lock);
    return bytes;
}
esp_err_t player_wav_error(esp_gmf_io_handle_t handle) {
    wav_io_t *io = handle;
    portENTER_CRITICAL(&io->lock);
    esp_err_t error = io->error;
    portEXIT_CRITICAL(&io->lock);
    return error;
}
static void le32(uint8_t *p, uint32_t n) {
    for (int i = 0; i < 4; ++i)
        p[i] = n >> (8 * i);
}
void yoke_voice_wav_header(uint8_t h[44], uint32_t n) {
    memset(h, 0, 44);
    memcpy(h, "RIFF", 4); le32(h + 4, n + 36);
    memcpy(h + 8, "WAVEfmt ", 8); le32(h + 16, 16);
    h[20] = 1; h[22] = 1;
    le32(h + 24, 16000); le32(h + 28, 32000);
    h[32] = 2; h[34] = 16;
    memcpy(h + 36, "data", 4); le32(h + 40, n);
}

esp_err_t yoke_voice_runtime_set_record_file(esp_gmf_pipeline_handle_t pipe, FILE *file) {
    if (!pipe || !file)
        return ESP_ERR_INVALID_ARG;
    esp_gmf_io_handle_t out = NULL;
    if (esp_gmf_pipeline_get_out(pipe, &out) != ESP_GMF_ERR_OK || !out ||
        strcmp(OBJ_GET_TAG(out), "io_wav_pcm"))
        return ESP_ERR_INVALID_ARG;
    player_wav_set_file(out, file);
    return ESP_OK;
}


esp_err_t yoke_voice_wav_status(esp_gmf_io_handle_t handle, uint32_t *bytes, esp_err_t *error) {
    if (!handle || !bytes || !error || strcmp(OBJ_GET_TAG(handle), "io_wav_pcm")) return ESP_ERR_INVALID_ARG;
    wav_io_t *io = handle;
    portENTER_CRITICAL(&io->lock);
    *bytes = io->bytes; *error = io->error;
    portEXIT_CRITICAL(&io->lock);
    return ESP_OK;
}
