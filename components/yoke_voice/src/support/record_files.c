#include <ctype.h>
#include <stdbool.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "yoke_voice_record.h"
#include "yoke_voice_io.h"

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)p[1] << 8; }
static esp_err_t slice_path(const yoke_voice_muxer_files_t *files, unsigned index, char *path, size_t capacity) {
    int n = snprintf(path, capacity, "%s_%03u.wav", files->prefix, index);
    return n >= 0 && (size_t)n < capacity ? ESP_OK : ESP_ERR_INVALID_SIZE;
}
esp_err_t yoke_voice_muxer_files_init(yoke_voice_muxer_files_t *files, const char *prefix) {
    if (!files || !prefix || prefix[0] != '/' || strlen(prefix) + sizeof("_000.wav") > sizeof(files->prefix) ||
        strstr(prefix, "..") || strchr(prefix, '\\'))
        return ESP_ERR_INVALID_ARG;
    const char *name = strrchr(prefix, '/') + 1;
    if (!*name)
        return ESP_ERR_INVALID_ARG;
    for (const char *c = name; *c; ++c)
        if (!isalnum((unsigned char)*c) && *c != '_' && *c != '-')
            return ESP_ERR_INVALID_ARG;
    memset(files, 0, sizeof(*files));
    strcpy(files->prefix, prefix);
    return ESP_OK;
}
esp_err_t yoke_voice_muxer_reserve_slice(yoke_voice_muxer_files_t *files, unsigned index, char *path, size_t capacity) {
    if (!files || !path || !capacity)
        return ESP_ERR_INVALID_ARG;
    if (files->error)
        return files->error;
    // 最长录音 30 秒、最短分片 1 秒；容许边界额外生成一个空尾片。
    esp_err_t ret = index == files->created && index < 32 ? slice_path(files, index, path, capacity) : ESP_ERR_INVALID_SIZE;
    if (ret == ESP_OK) {
        int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (fd < 0)
            ret = ESP_FAIL;
        else {
            ++files->created;
            if (close(fd))
                ret = ESP_FAIL;
        }
    }
    files->error = ret;
    return ret;
}
void yoke_voice_muxer_remove_files(yoke_voice_muxer_files_t *files) {
    if (!files)
        return;
    char path[128];
    for (unsigned i = 0; i < files->created; ++i)
        if (slice_path(files, i, path, sizeof(path)) == ESP_OK)
            unlink(path);
}
esp_err_t yoke_voice_wav_data_size(const char *path, uint32_t *bytes) {
    if (!path || !bytes)
        return ESP_ERR_INVALID_ARG;
    *bytes = 0;
    struct stat st;
    if (stat(path, &st) || st.st_size < 44 || (uint64_t)st.st_size > UINT32_MAX)
        return ESP_ERR_INVALID_SIZE;
    FILE *f = fopen(path, "rb");
    if (!f)
        return ESP_FAIL;
    esp_err_t ret = ESP_ERR_INVALID_SIZE;
    uint8_t header[12];
    if (fread(header, 1, sizeof(header), f) != sizeof(header) || memcmp(header, "RIFF", 4) ||
        memcmp(header + 8, "WAVE", 4) || (uint64_t)le32(header + 4) + 8 != (uint64_t)st.st_size)
        goto done;
    bool fmt = false, data = false;
    uint64_t pos = 12;
    while (pos + 8 <= (uint64_t)st.st_size) {
        uint8_t chunk[8];
        if (fread(chunk, 1, sizeof(chunk), f) != sizeof(chunk))
            goto done;
        uint32_t size = le32(chunk + 4);
        uint64_t end = pos + 8 + size + (size & 1);
        if (end > (uint64_t)st.st_size)
            goto done;
        if (!memcmp(chunk, "fmt ", 4)) {
            uint8_t format[16];
            if (fmt || size < sizeof(format) || fread(format, 1, sizeof(format), f) != sizeof(format) ||
                le16(format) != 1 || le16(format + 2) != 1 || le32(format + 4) != 16000 ||
                le32(format + 8) != 32000 || le16(format + 12) != 2 || le16(format + 14) != 16)
                goto done;
            fmt = true;
        } else if (!memcmp(chunk, "data", 4)) {
            if (data || (size & 1))
                goto done;
            *bytes = size;
            data = true;
        }
        if (fseek(f, (long)end, SEEK_SET))
            goto done;
        pos = end;
    }
    if (fmt && data && pos == (uint64_t)st.st_size)
        ret = ESP_OK;
done:
    if (fclose(f))
        ret = ESP_FAIL;
    if (ret != ESP_OK)
        *bytes = 0;
    return ret;
}
esp_err_t yoke_voice_muxer_validate_files(const yoke_voice_muxer_files_t *files, uint32_t frames, uint32_t *pcm_bytes) {
    if (!files || !pcm_bytes)
        return ESP_ERR_INVALID_ARG;
    *pcm_bytes = 0;
    if (files->error)
        return files->error;
    if (!frames || !files->created)
        return ESP_ERR_NOT_FOUND;
    char path[128];
    uint64_t total = 0;
    for (unsigned i = 0; i < files->created; ++i) {
        uint32_t bytes;
        esp_err_t ret = slice_path(files, i, path, sizeof(path));
        if (ret == ESP_OK)
            ret = yoke_voice_wav_data_size(path, &bytes);
        if (ret != ESP_OK)
            return ret;
        total += bytes;
    }
    if (total > UINT32_MAX || total != (uint64_t)frames * YOKE_VOICE_RECORD_PCM_BYTES)
        return ESP_ERR_INVALID_SIZE;
    *pcm_bytes = total;
    return ESP_OK;
}
