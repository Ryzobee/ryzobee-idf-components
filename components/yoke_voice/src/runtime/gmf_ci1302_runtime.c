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
#include "gmf_loader_setup_defaults.h"
#include "esp_gmf_io_http.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
static inline bool player_gmf_cancelled(const yoke_voice_control_t *control) {
    return control && control->cancelled && control->cancelled(control->ctx);
}
static void gmf_ci1302_runtime_deinit(yoke_voice_runtime_t *ctx);
#if CONFIG_YOKE_VOICE_HTTP
static int http_event(http_stream_event_msg_t *msg) {
    if (msg->event_id == HTTP_STREAM_PRE_REQUEST)
        return esp_http_client_set_timeout_ms(msg->http_client, 2000);
    return ESP_GMF_ERR_OK;
}
#endif
static esp_err_t gmf_ci1302_runtime_init(yoke_voice_runtime_t *ctx, yoke_voice_device_handle_t dev) {
    if (!ctx || !dev || ctx->pool || ctx->session)
        return ESP_ERR_INVALID_ARG;
    if (CONFIG_GMF_AUDIO_EFFECT_CH_CVT_DEST_CH != 1 ||
        CONFIG_GMF_AUDIO_EFFECT_BIT_CVT_DEST_BITS != 16 ||
        CONFIG_GMF_AUDIO_EFFECT_RATE_CVT_DEST_RATE != 16000)
        return ESP_ERR_NOT_SUPPORTED;
    ctx->device = dev;
    ctx->session = xSemaphoreCreateMutex();
    if (!ctx->session)
        return ESP_ERR_NO_MEM;
    if (esp_gmf_pool_init(&ctx->pool) != ESP_GMF_ERR_OK)
        goto fail;
    /* Loader 根据 menuconfig 注册 IO、解码和格式转换节点的原型。 */
    if (gmf_loader_setup_io_default(ctx->pool) != ESP_GMF_ERR_OK)
        goto fail;
    if (gmf_loader_setup_audio_codec_default(ctx->pool) != ESP_GMF_ERR_OK)
        goto fail;
    ctx->codecs_loaded = true;
    if (gmf_loader_setup_audio_muxer_default(ctx->pool) != ESP_GMF_ERR_OK)
        goto fail;
    ctx->muxers_loaded = true;
    if (gmf_loader_setup_audio_effects_default(ctx->pool) != ESP_GMF_ERR_OK ||
        yoke_voice_io_register(ctx->pool, dev) != ESP_GMF_ERR_OK ||
        player_wav_register(ctx->pool) != ESP_GMF_ERR_OK)
        goto fail;
    /* HTTP IO 是 BLOCK 类型，必须保留默认读取线程和数据总线缓冲区。
     * 数据总线为读取回调提供有效 payload.buf；清零线程/缓冲区会导致空指针写入。
     * Loader 的 HTTP 原型在 Kconfig 中关闭，避免相同标签重复注册。 */
#if CONFIG_YOKE_VOICE_HTTP
    http_io_cfg_t http = HTTP_STREAM_CFG_DEFAULT();
    http.crt_bundle_attach = esp_crt_bundle_attach;
    http.event_handle = http_event;
    esp_gmf_io_handle_t io = NULL;
    if (esp_gmf_io_http_init(&http, &io) != ESP_GMF_ERR_OK)
        goto fail;
    if (esp_gmf_pool_register_io(ctx->pool, io, "io_http") != ESP_GMF_ERR_OK) {
        esp_gmf_obj_delete(io);
        goto fail;
    }
#endif
    return ESP_OK;
fail:
    gmf_ci1302_runtime_deinit(ctx);
    return ESP_FAIL;
}
static void gmf_ci1302_runtime_deinit(yoke_voice_runtime_t *ctx) {
    if (!ctx)
        return;
    if (ctx->pool) {
        if (ctx->muxers_loaded)
            gmf_loader_teardown_audio_muxer_default(ctx->pool);
        gmf_loader_teardown_audio_effects_default(ctx->pool);
        if (ctx->codecs_loaded)
            gmf_loader_teardown_audio_codec_default(ctx->pool);
        gmf_loader_teardown_io_default(ctx->pool);
        esp_gmf_pool_deinit(ctx->pool);
    }
    if (ctx->session)
        vSemaphoreDelete(ctx->session);
    memset(ctx, 0, sizeof(*ctx));
}

esp_err_t yoke_voice_runtime_session_begin(yoke_voice_runtime_t *ctx, const yoke_voice_control_t *control) {
    if (!ctx || !ctx->pool || !ctx->session)
        return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(ctx->session, 0) != pdTRUE)
        return ESP_ERR_INVALID_STATE;
    if (player_gmf_cancelled(control)) {
        xSemaphoreGive(ctx->session);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}
void yoke_voice_runtime_session_end(yoke_voice_runtime_t *ctx) {
    xSemaphoreGive(ctx->session);
}

static void gmf_ci1302_runtime_deinit(yoke_voice_runtime_t *ctx);
esp_err_t yoke_voice_runtime_create(yoke_voice_device_handle_t device, yoke_voice_runtime_t **out) {
    if (!device || !out) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    yoke_voice_runtime_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return ESP_ERR_NO_MEM;
    esp_err_t ret = gmf_ci1302_runtime_init(ctx, device);
    if (ret != ESP_OK) { free(ctx); return ret; }
    *out = ctx;
    return ESP_OK;
}
void yoke_voice_runtime_destroy(yoke_voice_runtime_t *ctx) {
    if (ctx) { gmf_ci1302_runtime_deinit(ctx); free(ctx); }
}
esp_gmf_pool_handle_t yoke_voice_runtime_pool(yoke_voice_runtime_t *ctx) {
    return ctx ? ctx->pool : NULL;
}

yoke_voice_device_handle_t yoke_voice_runtime_device(yoke_voice_runtime_t *ctx) {
    return ctx ? ctx->device : NULL;
}
