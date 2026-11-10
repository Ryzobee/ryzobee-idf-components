#pragma once
#include "yoke_voice_runtime.h"

/* 示例在后台任务中运行至结束；主循环通过 yoke_voice_* 异步接口选择示例。 */
esp_err_t example_play_file(yoke_voice_runtime_t *ctx, const char *path, const yoke_voice_control_t *control);
esp_err_t example_play_http(yoke_voice_runtime_t *ctx, const char *url, const yoke_voice_control_t *control);
esp_err_t example_record(yoke_voice_runtime_t *ctx, FILE *file, unsigned seconds, const yoke_voice_control_t *control);
/* Muxer 直接生成分片 WAV，prefix 不包含序号后缀或扩展名。 */
esp_err_t example_record_audio_muxer(yoke_voice_runtime_t *ctx, const char *prefix,
                                        unsigned seconds, unsigned slice_ms,
                                        const yoke_voice_control_t *control);

esp_err_t example_run(yoke_voice_runtime_t *ctx, esp_gmf_pipeline_handle_t pipe, unsigned seconds, const yoke_voice_control_t *control);
esp_err_t example_run_muxer(yoke_voice_runtime_t *ctx, esp_gmf_pipeline_handle_t pipe, unsigned seconds, const yoke_voice_control_t *control, uint32_t *frames);
