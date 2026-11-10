#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 每个会话独占一个前缀。仅记录成功 O_EXCL 创建的分片，失败时不删除别人的文件。 */
typedef struct {
    char prefix[128];
    unsigned created;
    esp_err_t error;
} yoke_voice_muxer_files_t;
esp_err_t yoke_voice_muxer_files_init(yoke_voice_muxer_files_t *files, const char *prefix);
esp_err_t yoke_voice_muxer_reserve_slice(yoke_voice_muxer_files_t *files, unsigned index, char *path, size_t capacity);
void yoke_voice_muxer_remove_files(yoke_voice_muxer_files_t *files);
/* 读取已关闭的 RIFF/WAV，校验格式、块边界、实际文件长度及录音包对应的 PCM 总量。 */
esp_err_t yoke_voice_wav_data_size(const char *path, uint32_t *bytes);
esp_err_t yoke_voice_muxer_validate_files(const yoke_voice_muxer_files_t *files, uint32_t frames, uint32_t *pcm_bytes);

#ifdef __cplusplus
}
#endif
