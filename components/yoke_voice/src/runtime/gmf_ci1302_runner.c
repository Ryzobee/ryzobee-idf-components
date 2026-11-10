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
#include "esp_gmf_audio_muxer.h"
static inline bool player_gmf_cancelled(const yoke_voice_control_t *control) {
    return control && control->cancelled && control->cancelled(control->ctx);
}
#define DONE BIT0
#define ERROR BIT1
static const char *TAG = "gmf_ci1302_gmf";

static esp_gmf_err_t pipeline_event(esp_gmf_event_pkt_t *evt, void *ctx) {
    if (evt->type == ESP_GMF_EVT_TYPE_CHANGE_STATE) {
        if (evt->sub == ESP_GMF_EVENT_STATE_ERROR)
            xEventGroupSetBits(ctx, ERROR);
        else if (evt->sub == ESP_GMF_EVENT_STATE_FINISHED)
            xEventGroupSetBits(ctx, DONE);
    }
    return ESP_GMF_ERR_OK;
}

/* 示例拥有流水线；运行器拥有本次任务和事件组。
 * 返回前停止任务、关闭 IO，调用方随后销毁流水线并释放会话锁。 */
static esp_err_t run_pipeline(yoke_voice_runtime_t *ctx, esp_gmf_pipeline_handle_t pipe,
                              unsigned record_seconds, bool muxer_file,
                              const yoke_voice_control_t *control, uint32_t *accepted_frames) {
    if (!ctx || !ctx->pool || !pipe)
        return ESP_ERR_INVALID_STATE;
    if (record_seconds > 30)
        return ESP_ERR_INVALID_ARG;
    bool record = record_seconds != 0;
    bool started = false;
    esp_err_t result = ESP_OK;
    esp_gmf_task_handle_t task = NULL;
    esp_gmf_io_handle_t in = NULL, out = NULL, hardware = NULL;
    EventGroupHandle_t events = xEventGroupCreate();
    yoke_voice_io_status_t status = {0};
    uint32_t pcm_bytes = 0;
    if (!events) {
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    if (player_gmf_cancelled(control)) {
        result = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }
    if (esp_gmf_pipeline_get_in(pipe, &in) != ESP_GMF_ERR_OK || !in ||
        esp_gmf_pipeline_get_out(pipe, &out) != ESP_GMF_ERR_OK || (!out && !muxer_file)) {
        result = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }
    // 此运行器服务于 CI1302 音频示例，避免把其他 IO 当作 CI1302/WAV 实例读取。
    if (strcmp(OBJ_GET_TAG((record ? in : out)), record ? "io_ci1302_rx" : "io_ci1302_tx") ||
        (record && !muxer_file && strcmp(OBJ_GET_TAG(out), "io_wav_pcm"))) {
        result = ESP_ERR_NOT_SUPPORTED;
        goto cleanup;
    }
    if (muxer_file) {
        esp_gmf_element_handle_t muxer = NULL;
        if (out || esp_gmf_pipeline_get_el_by_name(pipe, "aud_muxer", &muxer) != ESP_GMF_ERR_OK) {
            result = ESP_ERR_INVALID_ARG;
            goto cleanup;
        }
        esp_gmf_audio_muxer_cfg_t *cfg = OBJ_GET_CFG(muxer);
        if (!cfg || cfg->output_type != ESP_GMF_AUDIO_MUXER_OUTPUT_FILE) {
            result = ESP_ERR_NOT_SUPPORTED;
            goto cleanup;
        }
    }
    hardware = record ? in : out;
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.name = "gmf_audio";
    cfg.thread.core = 0;
    cfg.thread.prio = 11;
    cfg.thread.stack = 12288;
    cfg.thread.stack_in_ext = false;
    if (esp_gmf_task_init(&cfg, &task) != ESP_GMF_ERR_OK ||
        esp_gmf_task_set_timeout(task, 20000) != ESP_GMF_ERR_OK ||
        esp_gmf_pipeline_bind_task(pipe, task) != ESP_GMF_ERR_OK ||
        esp_gmf_pipeline_set_event(pipe, pipeline_event, events) != ESP_GMF_ERR_OK ||
        esp_gmf_pipeline_loading_jobs(pipe) != ESP_GMF_ERR_OK) {
        result = ESP_FAIL;
        goto cleanup;
    }
    if (esp_gmf_pipeline_run(pipe) != ESP_GMF_ERR_OK) {
        result = ESP_FAIL;
        goto cleanup;
    }
    started = true;
    int64_t start = esp_timer_get_time(), drain_start = 0, progress_time = start;
    uint32_t previous = 0;
    for (;;) {
        yoke_voice_io_status(hardware, &status);
        /* WAV 计数通过应用层输出 IO 的线程安全状态接口读取。 */
        pcm_bytes = record ? (muxer_file ? 0 : player_wav_bytes(out)) : status.submitted_bytes;
        if (control && control->progress)
            control->progress(control->ctx, pcm_bytes);
        if (status.error || (record && !muxer_file && player_wav_error(out))) {
            result = status.error ? status.error : player_wav_error(out);
            break;
        }
        EventBits_t bits = xEventGroupGetBits(events);
        if (bits & ERROR) {
            result = ESP_FAIL;
            break;
        }
        bool stop = player_gmf_cancelled(control);
        int64_t now = esp_timer_get_time();
        if (!record && stop) {
            result = ESP_ERR_INVALID_STATE;
            break;
        }
        if (record && !drain_start && (stop || status.eof || now - start >= record_seconds * 1000000LL)) {
            yoke_voice_io_finish(hardware);
            drain_start = now;
        }
        if (bits & DONE)
            break;
        if (previous != pcm_bytes) {
            previous = pcm_bytes;
            progress_time = now;
        }
        if ((record && drain_start && now - drain_start > 5000000LL) ||
            (!record && now - progress_time > 10000000LL)) {
            result = ESP_ERR_TIMEOUT;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
cleanup:
    if (hardware && result != ESP_OK)
        yoke_voice_io_cancel(hardware);
    if (in)
        esp_gmf_io_abort(in);
    if (out)
        esp_gmf_io_abort(out);
    if (pipe && task) {
        if (esp_gmf_pipeline_stop(pipe) != ESP_GMF_ERR_OK && result == ESP_OK)
            result = ESP_ERR_TIMEOUT;
    }
    if (task)
        esp_gmf_task_deinit(task);
    /* 任务退出后再读最终计数、关闭 IO，最后销毁流水线及事件组。 */
    if (in)
        esp_gmf_io_close(in);
    if (out)
        esp_gmf_io_close(out);
    if (hardware) {
        yoke_voice_io_status(hardware, &status);
        if (result == ESP_OK && status.error)
            result = status.error;
        pcm_bytes = record ? (muxer_file ? 0 : player_wav_bytes(out)) : status.submitted_bytes;
        if (control && control->progress)
            control->progress(control->ctx, pcm_bytes);
    }
    if (record && started) {
        esp_err_t quiet = yoke_voice_device_wait_quiet(ctx->device, 2000);
        if (result == ESP_OK && quiet != ESP_OK)
            result = quiet;
        yoke_voice_io_status(hardware, &status);
        if (result == ESP_OK && status.error)
            result = status.error;
        if (result == ESP_OK && !muxer_file && pcm_bytes != status.accepted_frames * YOKE_VOICE_RECORD_PCM_BYTES)
            result = ESP_ERR_INVALID_SIZE;
        if (muxer_file)
            ESP_LOGI(TAG, "muxer input packets=%lu; WAV PCM verified after file close", (unsigned long)status.accepted_frames);
        else
            ESP_LOGI(TAG, "record packets=%lu PCM=%lu", (unsigned long)status.accepted_frames, (unsigned long)pcm_bytes);
    } else if (!record && started && result == ESP_OK) {
        int64_t end = esp_timer_get_time() + 200000;
        while (esp_timer_get_time() < end) {
            if (player_gmf_cancelled(control)) {
                result = ESP_ERR_INVALID_STATE;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (accepted_frames)
        *accepted_frames = status.accepted_frames;
    if (result == ESP_OK && (muxer_file ? !status.accepted_frames : !pcm_bytes))
        result = ESP_ERR_NOT_FOUND;
    if (events)
        vEventGroupDelete(events);
    return result;
}

esp_err_t yoke_voice_runtime_run(yoke_voice_runtime_t *ctx, esp_gmf_pipeline_handle_t pipe,
                              unsigned record_seconds, const yoke_voice_control_t *control) {
    return run_pipeline(ctx, pipe, record_seconds, false, control, NULL);
}
esp_err_t yoke_voice_runtime_run_muxer(yoke_voice_runtime_t *ctx, esp_gmf_pipeline_handle_t pipe,
                                    unsigned seconds, const yoke_voice_control_t *control,
                                    uint32_t *accepted_frames) {
    if (!accepted_frames || !seconds || seconds > 30)
        return ESP_ERR_INVALID_ARG;
    *accepted_frames = 0;
    return run_pipeline(ctx, pipe, seconds, true, control, accepted_frames);
}
