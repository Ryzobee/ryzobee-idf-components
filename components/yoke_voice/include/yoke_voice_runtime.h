#pragma once
#include <stdio.h>
#include "esp_err.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_pipeline.h"
#include "yoke_voice_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct yoke_voice_runtime yoke_voice_runtime_t;
typedef struct {
    bool (*cancelled)(void *ctx);
    void (*progress)(void *ctx, uint32_t pcm_bytes);
    void *ctx;
} yoke_voice_control_t;

/* Optional default Pool/Loader environment. Does not initialize hardware or networking. */
esp_err_t yoke_voice_runtime_create(yoke_voice_device_handle_t device, yoke_voice_runtime_t **out);
esp_gmf_pool_handle_t yoke_voice_runtime_pool(yoke_voice_runtime_t *ctx);
void yoke_voice_runtime_destroy(yoke_voice_runtime_t *ctx);
/* 示例先取得会话，再组链。必须在每条退出路径调用 session_end。 */
esp_err_t yoke_voice_runtime_session_begin(yoke_voice_runtime_t *ctx, const yoke_voice_control_t *control);
void yoke_voice_runtime_session_end(yoke_voice_runtime_t *ctx);
/* 执行已配置的流水线；返回前停止任务并关闭 IO，但不销毁 pipe。
 * record_seconds=0 表示播放；1～30 表示录音并在停止时排空已接收数据。 */
esp_err_t yoke_voice_runtime_run(yoke_voice_runtime_t *ctx, esp_gmf_pipeline_handle_t pipe,
                              unsigned record_seconds, const yoke_voice_control_t *control);
/* aud_muxer 文件输出没有末端 IO；实际 PCM 数量由示例在封装文件关闭后验证。
 * accepted_frames 返回 CI1302 已接收包数，供 WAV 数据长度核对使用。 */
esp_err_t yoke_voice_runtime_run_muxer(yoke_voice_runtime_t *ctx, esp_gmf_pipeline_handle_t pipe,
                                    unsigned seconds, const yoke_voice_control_t *control,
                                    uint32_t *accepted_frames);
/* WAV IO 借用调用方的 FILE；文件头回填和 fclose 由应用负责。 */
esp_err_t yoke_voice_runtime_set_record_file(esp_gmf_pipeline_handle_t pipe, FILE *file);
void yoke_voice_wav_header(uint8_t header[44], uint32_t pcm_bytes);


/* Caller must keep the runtime alive and idle during destruction. */
yoke_voice_device_handle_t yoke_voice_runtime_device(yoke_voice_runtime_t *ctx);
/* Read statistics from an io_wav_pcm instance after checking its tag. */
esp_err_t yoke_voice_wav_status(esp_gmf_io_handle_t io, uint32_t *bytes, esp_err_t *error);

#ifdef __cplusplus
}
#endif
