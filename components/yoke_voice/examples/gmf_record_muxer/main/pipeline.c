/* SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: Apache-2.0
 *
 * 参考 ESP-GMF 提交 17ce627cd547d94acfc14890807dbccdc8b459c6 的
 * pipeline_record_audio_muxer。CI1302 上传 Opus，需要先解码；使用 LittleFS 替代 SD 卡。
 */
#include "example.h"
#include "yoke_voice_record.h"
#include "esp_gmf_audio_dec.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_audio_muxer.h"
#include "esp_opus_dec.h"
#include "esp_log.h"

static int file_pattern(esp_muxer_slice_info_t *info, void *ctx) {
    if (!info || info->slice_index < 0 || info->len <= 0)
        return -1;
    // Muxer 自行打开、写入并关闭文件；这里只独占预留文件名。
    esp_err_t ret = yoke_voice_muxer_reserve_slice(ctx, info->slice_index, info->file_path, info->len);
    if (ret == ESP_OK)
        ESP_LOGI("record_muxer", "slice %d: %s", info->slice_index, info->file_path);
    return ret == ESP_OK ? 0 : -1;
}

/* Opus → PCM → 格式转换 → aud_enc(PCM) → aud_muxer(WAV, FILE)。
 * FILE 模式由 muxer 直接写文件，因此 pipeline 的输出 IO 为 NULL。
 * prefix 示例：/recordings/mux_0001，生成 mux_0001_000.wav、mux_0001_001.wav 等。
 */
esp_err_t example_record_audio_muxer(yoke_voice_runtime_t *ctx, const char *prefix,
                                        unsigned seconds, unsigned slice_ms,
                                        const yoke_voice_control_t *control) {
    if (!seconds || seconds > 30 || slice_ms < 1000 || slice_ms > 60000)
        return ESP_ERR_INVALID_ARG;
    yoke_voice_muxer_files_t files = {0};
    esp_err_t result = yoke_voice_muxer_files_init(&files, prefix);
    if (result != ESP_OK)
        return result;
    result = yoke_voice_runtime_session_begin(ctx, control);
    if (result != ESP_OK)
        return result;
    esp_gmf_pipeline_handle_t pipe = NULL;
    uint32_t frames = 0, pcm_bytes = 0;
    const char *elements[] = {"aud_dec", "aud_ch_cvt", "aud_bit_cvt", "aud_rate_cvt", "aud_enc", "aud_muxer"};
    if (esp_gmf_pool_new_pipeline(yoke_voice_runtime_pool(ctx), "io_ci1302_rx", elements, 6, NULL, &pipe) != ESP_GMF_ERR_OK) {
        result = ESP_FAIL;
        goto cleanup;
    }
    esp_gmf_element_handle_t decoder = NULL, encoder = NULL, muxer = NULL;
    if (esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &decoder) != ESP_GMF_ERR_OK ||
        esp_gmf_pipeline_get_el_by_name(pipe, "aud_enc", &encoder) != ESP_GMF_ERR_OK ||
        esp_gmf_pipeline_get_el_by_name(pipe, "aud_muxer", &muxer) != ESP_GMF_ERR_OK) {
        result = ESP_FAIL;
        goto cleanup;
    }
    esp_opus_dec_cfg_t opus = ESP_OPUS_DEC_CONFIG_DEFAULT();
    opus.sample_rate = 16000;
    opus.channel = 1;
    opus.frame_duration = ESP_OPUS_DEC_FRAME_DURATION_40_MS;
    esp_audio_simple_dec_cfg_t dec = DEFAULT_ESP_GMF_AUDIO_DEC_CONFIG();
    dec.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_RAW_OPUS;
    dec.use_frame_dec = true;
    dec.dec_cfg = &opus;
    dec.cfg_size = sizeof(opus);
    esp_gmf_info_sound_t info = {.sample_rates = 16000, .channels = 1, .bits = 16, .format_id = ESP_AUDIO_TYPE_PCM};
    if (esp_gmf_audio_dec_reconfig(decoder, &dec) != ESP_GMF_ERR_OK ||
        esp_gmf_audio_enc_reconfig_by_sound_info(encoder, &info) != ESP_GMF_ERR_OK) {
        result = ESP_FAIL;
        goto cleanup;
    }
    // 编码器输入格式由解码器及转换节点在运行时传播，不能把 PCM 信息报给 Opus 解码器。
    esp_gmf_audio_muxer_cfg_t *cfg = OBJ_GET_CFG(muxer);
    cfg->muxer_type = ESP_MUXER_TYPE_WAV;
    cfg->codec = ESP_MUXER_ADEC_PCM;
    cfg->output_type = ESP_GMF_AUDIO_MUXER_OUTPUT_FILE;
    cfg->slice_duration = slice_ms;
    cfg->url_pattern = file_pattern;
    cfg->url_ctx = &files;
    cfg->get_codec_spec_info_cb = NULL; // PCM 不需要 ALAC 等格式的 codec specific info。
    cfg->get_codec_spec_info_ctx = NULL;
    result = example_run_muxer(ctx, pipe, seconds, control, &frames);
cleanup:
    if (pipe)
        esp_gmf_pipeline_destroy(pipe);
    if (result == ESP_OK)
        result = yoke_voice_muxer_validate_files(&files, frames, &pcm_bytes);
    if (result == ESP_OK) {
        if (control && control->progress)
            control->progress(control->ctx, pcm_bytes);
        ESP_LOGI("record_muxer", "%u WAV slice(s), PCM=%lu", files.created, (unsigned long)pcm_bytes);
    } else
        yoke_voice_muxer_remove_files(&files);
    yoke_voice_runtime_session_end(ctx);
    return result;
}
