#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "yoke_voice_record.h"

static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = v >> (8 * i);
}
/* 生成真实 RIFF/WAV 测试文件，可带有非音频 JUNK 块及奇数长度填充。 */
static void fixture(const char *path, unsigned bytes, bool junk) {
    uint8_t header[44] = {0};
    memcpy(header, "RIFF", 4); put32(header + 4, 36 + bytes + (junk ? 12 : 0));
    memcpy(header + 8, "WAVEfmt ", 8); put32(header + 16, 16);
    header[20] = 1; header[22] = 1;
    put32(header + 24, 16000); put32(header + 28, 32000);
    header[32] = 2; header[34] = 16;
    memcpy(header + 36, "data", 4); put32(header + 40, bytes);
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite(header, 1, 36, f) == 36);
    if (junk) {
        const uint8_t extra[12] = {'J','U','N','K',3,0,0,0,1,2,3,0};
        assert(fwrite(extra, 1, sizeof(extra), f) == sizeof(extra));
    }
    assert(fwrite(header + 36, 1, 8, f) == 8);
    for (unsigned i = 0; i < bytes; ++i)
        assert(fputc(i & 0xff, f) != EOF);
    assert(fclose(f) == 0);
}
static void mutate32(const char *path, long pos, uint32_t value) {
    FILE *f = fopen(path, "r+b"); assert(f);
    uint8_t data[4]; put32(data, value);
    assert(fseek(f, pos, SEEK_SET) == 0);
    assert(fwrite(data, 1, sizeof(data), f) == sizeof(data));
    assert(fclose(f) == 0);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    char prefix[128], first[128], second[128];
    snprintf(prefix, sizeof(prefix), "%s/mux", argv[1]);
    yoke_voice_muxer_files_t files;
    assert(yoke_voice_muxer_files_init(&files, "/tmp/../bad") == ESP_ERR_INVALID_ARG);
    assert(yoke_voice_muxer_files_init(&files, "/tmp/") == ESP_ERR_INVALID_ARG);
    assert(yoke_voice_muxer_files_init(&files, prefix) == ESP_OK);
    assert(yoke_voice_muxer_reserve_slice(&files, 0, first, sizeof(first)) == ESP_OK);
    assert(yoke_voice_muxer_reserve_slice(&files, 1, second, sizeof(second)) == ESP_OK);
    fixture(first, 1280, false); fixture(second, 1280, true);
    uint32_t bytes = 0;
    assert(yoke_voice_muxer_validate_files(&files, 2, &bytes) == ESP_OK && bytes == 2560);
    assert(yoke_voice_muxer_validate_files(&files, 3, &bytes) == ESP_ERR_INVALID_SIZE);
    mutate32(second, 24, 48000);
    assert(yoke_voice_wav_data_size(second, &bytes) == ESP_ERR_INVALID_SIZE);
    fixture(second, 1280, true);
    mutate32(second, 52, UINT32_MAX);
    assert(yoke_voice_wav_data_size(second, &bytes) == ESP_ERR_INVALID_SIZE);
    fixture(second, 1280, false);
    assert(truncate(second, 1322) == 0);
    assert(yoke_voice_wav_data_size(second, &bytes) == ESP_ERR_INVALID_SIZE);
    fixture(second, 0, false);
    assert(yoke_voice_muxer_validate_files(&files, 1, &bytes) == ESP_OK && bytes == 1280);
    yoke_voice_muxer_remove_files(&files);
    assert(access(first, F_OK) != 0 && access(second, F_OK) != 0);
    // 第二片原已存在时，失败清理只删除本次创建的第一片。
    assert(yoke_voice_muxer_files_init(&files, prefix) == ESP_OK);
    assert(yoke_voice_muxer_reserve_slice(&files, 0, first, sizeof(first)) == ESP_OK);
    fixture(second, 1280, false);
    assert(yoke_voice_muxer_reserve_slice(&files, 1, second, sizeof(second)) != ESP_OK);
    assert(files.created == 1);
    yoke_voice_muxer_remove_files(&files);
    assert(access(first, F_OK) != 0 && access(second, F_OK) == 0);
    assert(yoke_voice_wav_data_size(second, &bytes) == ESP_OK && bytes == 1280);
    assert(unlink(second) == 0);
    assert(yoke_voice_muxer_files_init(&files, prefix) == ESP_OK);
    assert(yoke_voice_muxer_reserve_slice(&files, 1, first, sizeof(first)) == ESP_ERR_INVALID_SIZE);
    assert(files.created == 0);
    puts("Muxer WAV slices, chunk boundaries, truncation, PCM totals and exclusive file cleanup: PASS");
}
