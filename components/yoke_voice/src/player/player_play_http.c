/* SPDX-License-Identifier: Apache-2.0
 * 参考 ESP-GMF v1.1 pipeline_play_http_music，输出适配为 CI1302。 */
#include <string.h>
#include "yoke_voice_network.h"
#include "player_internal.h"
#include "esp_gmf_audio_dec.h"

/* 仅使用应用已连接的网络（未联网立即报错）→ HTTP MP3 → 解码 → 格式转换 → CI1302。 */
esp_err_t gmf_ci1302_pipeline_play_http(yoke_voice_runtime_t *ctx, const char *url, const yoke_voice_control_t *control) {
    if (!url || (strncmp(url, "http://", 7) && strncmp(url, "https://", 8)))
        return ESP_ERR_INVALID_ARG;
    esp_err_t result = yoke_voice_runtime_session_begin(ctx, control);
    if (result != ESP_OK)
        return result;

    esp_gmf_pipeline_handle_t pipe = NULL;
    result = yoke_voice_network_check();
    if (result != ESP_OK)
        goto cleanup;

    const char *elements[] = {"aud_dec", "aud_ch_cvt", "aud_bit_cvt", "aud_rate_cvt"};
    if (esp_gmf_pool_new_pipeline(yoke_voice_runtime_pool(ctx), "io_http", elements, 4, "io_ci1302_tx", &pipe) != ESP_GMF_ERR_OK) {
        result = ESP_FAIL;
        goto cleanup;
    }
    esp_gmf_element_handle_t decoder = NULL;
    esp_audio_simple_dec_cfg_t dec = DEFAULT_ESP_GMF_AUDIO_DEC_CONFIG();
    dec.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    if (esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &decoder) != ESP_GMF_ERR_OK ||
        esp_gmf_audio_dec_reconfig(decoder, &dec) != ESP_GMF_ERR_OK ||
        esp_gmf_pipeline_set_in_uri(pipe, url) != ESP_GMF_ERR_OK) {
        result = ESP_FAIL;
        goto cleanup;
    }
    result = yoke_voice_runtime_run(ctx, pipe, 0, control);
cleanup:
    if (pipe)
        esp_gmf_pipeline_destroy(pipe);
    yoke_voice_runtime_session_end(ctx);
    return result;
}
