/* SPDX-License-Identifier: Apache-2.0 */
#include "player_internal.h"
#include "esp_gmf_audio_dec.h"
#include "esp_opus_dec.h"

/* CI1302 Opus → 解码 → 单声道 / 16 位 / 16 kHz → 应用层 WAV 输出。 */
esp_err_t gmf_ci1302_pipeline_record(yoke_voice_runtime_t *ctx, FILE *file, unsigned seconds, const yoke_voice_control_t *control) {
    if (!file || !seconds || seconds > 30)
        return ESP_ERR_INVALID_ARG;
    esp_err_t result = yoke_voice_runtime_session_begin(ctx, control);
    if (result != ESP_OK)
        return result;

    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *elements[] = {"aud_dec", "aud_ch_cvt", "aud_bit_cvt", "aud_rate_cvt"};
    if (esp_gmf_pool_new_pipeline(yoke_voice_runtime_pool(ctx), "io_ci1302_rx", elements, 4, "io_wav_pcm", &pipe) != ESP_GMF_ERR_OK) {
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
    esp_gmf_element_handle_t decoder = NULL;
    if (esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &decoder) != ESP_GMF_ERR_OK ||
        esp_gmf_audio_dec_reconfig(decoder, &dec) != ESP_GMF_ERR_OK ||
        yoke_voice_runtime_set_record_file(pipe, file) != ESP_OK) {
        result = ESP_FAIL;
        goto cleanup;
    }
    result = yoke_voice_runtime_run(ctx, pipe, seconds, control);
cleanup:
    if (pipe)
        esp_gmf_pipeline_destroy(pipe);
    yoke_voice_runtime_session_end(ctx);
    return result;
}
