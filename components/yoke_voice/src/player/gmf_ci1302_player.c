#include <ctype.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "yoke_voice_player.h"
#include "yoke_voice_runtime.h"
#include "freertos/task.h"
#include "player_internal.h"
#include "sdkconfig.h"
#if CONFIG_YOKE_VOICE_HTTP
#include "yoke_voice_network.h"
#endif

#define GMF_CI1302_RECORDINGS_PATH "/recordings"
#define AUDIO_RESOURCE_PATH "/littlefs"

typedef struct {
    SemaphoreHandle_t api;
    TaskHandle_t control;
    portMUX_TYPE lock;
    yoke_voice_state_t state;
    bool record, http, stop, muxer;
    esp_err_t last_result;
    unsigned seconds;
    uint32_t bytes;
    char path[512];
    FILE *wav;
    yoke_voice_runtime_t *gmf;
    yoke_voice_player_config_t config;
    yoke_voice_event_cb_t callback;
} player_t;

static player_t player = {.lock = portMUX_INITIALIZER_UNLOCKED};
static inline void lock(void) { portENTER_CRITICAL(&player.lock); }
static inline void unlock(void) { portEXIT_CRITICAL(&player.lock); }

static bool cancelled(void *ctx) {
    (void)ctx;
    lock();
    bool stop = player.stop;
    unlock();
    return stop;
}
static void progress(void *ctx, uint32_t bytes) {
    (void)ctx;
    lock();
    player.bytes = bytes;
    if (player.record && player.stop)
        player.state = YOKE_VOICE_STOPPING;
    unlock();
}
static void controller(void *arg) {
    (void)arg;
    const yoke_voice_control_t control = {.cancelled = cancelled, .progress = progress};
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        esp_err_t result;
        if (player.record && player.muxer) {
            result = gmf_ci1302_pipeline_record_audio_muxer(player.gmf, player.path, player.seconds,
                                                     player.config.muxer_slice_ms, &control);
        } else if (player.record) {
            uint8_t header[44];
            yoke_voice_wav_header(header, 0);
            result = fwrite(header, 1, sizeof(header), player.wav) == sizeof(header) ? ESP_OK : ESP_FAIL;
            if (result == ESP_OK)
                result = gmf_ci1302_pipeline_record(player.gmf, player.wav, player.seconds, &control);
            yoke_voice_wav_header(header, player.bytes);
            if (fseek(player.wav, 0, SEEK_SET) || fwrite(header, 1, sizeof(header), player.wav) != sizeof(header) || fflush(player.wav))
                result = ESP_FAIL;
            if (fclose(player.wav))
                result = ESP_FAIL;
            player.wav = NULL;
            if (result != ESP_OK)
                unlink(player.path);
        } else if (player.http) {
#if CONFIG_YOKE_VOICE_HTTP
            result = gmf_ci1302_pipeline_play_http(player.gmf, player.path, &control);
#else
            result = ESP_ERR_NOT_SUPPORTED;
#endif
        } else {
            result = gmf_ci1302_pipeline_play_file(player.gmf, player.path, &control);
        }
        /* 完成回调前恢复空闲，允许回调提交下一个会话。 */
        xSemaphoreTake(player.api, portMAX_DELAY);
        bool recording = player.record;
        char path[sizeof(player.path)];
        if (player.muxer)
            snprintf(path, sizeof(path), "%.120s_000.wav", player.path);
        else
            strcpy(path, player.path);
        lock();
        player.last_result = result;
        player.state = YOKE_VOICE_IDLE;
        unlock();
        xSemaphoreGive(player.api);
        if (player.callback)
            player.callback(recording, result, path);
    }
}
esp_err_t yoke_voice_player_init(const yoke_voice_player_config_t *config) {
    if (!config || !config->device || config->muxer_slice_ms < 1000 || config->muxer_slice_ms > 60000)
        return ESP_ERR_INVALID_ARG;
    if (player.control) return ESP_ERR_INVALID_STATE;
    player.api = xSemaphoreCreateMutex();
    if (!player.api) return ESP_ERR_NO_MEM;
    player.config = *config;
    esp_err_t ret = yoke_voice_runtime_create(config->device, &player.gmf);
    if (ret != ESP_OK) goto fail;
    player.callback = config->callback;
    if (xTaskCreatePinnedToCore(controller, "audio_control", 8192, NULL, 12, &player.control, 0) != pdPASS) {
        ret = ESP_ERR_NO_MEM;
        yoke_voice_runtime_destroy(player.gmf);
        player.gmf = NULL;
        goto fail;
    }
    return ESP_OK;
fail:
    vSemaphoreDelete(player.api); player.api = NULL;
    return ret;
}
static esp_err_t admit(const char *path, bool record, bool http, unsigned seconds, bool muxer) {
    if (!player.control)
        return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(player.api, portMAX_DELAY);
    lock();
    bool busy = player.state != YOKE_VOICE_IDLE;
    unlock();
    esp_err_t ret = ESP_OK;
    if (busy || (record && !yoke_voice_device_quiet(player.config.device)))
        ret = ESP_ERR_INVALID_STATE;
    if (ret == ESP_OK && record) {
        size_t overhead = muxer ? 65536U + (seconds * 1000U / player.config.muxer_slice_ms + 1) * 4096U : 8192U;
        if (player.config.check_space)
            ret = player.config.check_space(player.config.space_ctx, seconds * 32000U + overhead);
        if (ret != ESP_OK) { /* application preflight rejected the request */ }
        else if (muxer) {
            char first[128];
            snprintf(first, sizeof(first), "%.110s_000.wav", path);
            FILE *existing = fopen(first, "rb");
            if (existing) {
                fclose(existing);
                ret = ESP_ERR_INVALID_STATE;
            }
            // 真正的独占文件名预留由 muxer 的分片回调使用 O_EXCL 完成。
        } else {
            int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
            if (fd < 0)
                ret = ESP_FAIL;
            else if (!(player.wav = fdopen(fd, "w+b"))) {
                close(fd);
                unlink(path);
                ret = ESP_FAIL;
            }
        }
    } else if (ret == ESP_OK && !http) {
        FILE *f = fopen(path, "rb");
        if (!f)
            ret = ESP_ERR_NOT_FOUND;
        else
            fclose(f);
    }
    if (ret == ESP_OK) {
        /* 写入请求后再发布任务通知；空闲前控制任务不接受第二个请求。 */
        player.record = record;
        player.http = http;
        player.muxer = muxer;
        player.seconds = seconds;
        strcpy(player.path, path);
        lock();
        player.stop = false;
        player.bytes = 0;
        player.state = record ? YOKE_VOICE_RECORDING : YOKE_VOICE_PLAYING;
        unlock();
        xTaskNotifyGive(player.control);
    }
    xSemaphoreGive(player.api);
    return ret;
}
esp_err_t yoke_voice_play_file(const char *path) {
    if (!path || path[0] != '/' || strlen(path) >= sizeof(player.path) || strstr(path, "..") || strchr(path, '\\'))
        return ESP_ERR_INVALID_ARG;
    const char *ext = strrchr(path, '.');
    if (!ext || (strcmp(ext, ".mp3") && strcmp(ext, ".wav"))) return ESP_ERR_INVALID_ARG;
    return admit(path, false, false, 0, false);
}
esp_err_t yoke_voice_play_http(const char *url) {
#if !CONFIG_YOKE_VOICE_HTTP
    (void)url; return ESP_ERR_NOT_SUPPORTED;
#else
    if (!url || strlen(url) >= sizeof(player.path) ||
        (strncmp(url, "http://", 7) && strncmp(url, "https://", 8)))
        return ESP_ERR_INVALID_ARG;
    if (!player.control) return ESP_ERR_INVALID_STATE;
    esp_err_t ret = yoke_voice_network_check();
    if (ret != ESP_OK) return ret;
    return admit(url, false, true, 0, false);
#endif
}
static bool record_path_valid(const char *path) {
    return path && path[0] == '/' && strlen(path) <= 110 && strlen(path) > 1 &&
        !strstr(path, "..") && !strchr(path, '\\') && path[strlen(path)-1] != '/';
}
esp_err_t yoke_voice_record_start(const char *path, unsigned seconds) {
    if (!record_path_valid(path) || seconds < 1 || seconds > 30) return ESP_ERR_INVALID_ARG;
    const char *ext = strrchr(path, '.');
    if (!ext || strcmp(ext, ".wav")) return ESP_ERR_INVALID_ARG;
    return admit(path, true, false, seconds, false);
}
esp_err_t yoke_voice_record_muxer_start(const char *prefix, unsigned seconds) {
    if (!record_path_valid(prefix) || seconds < 1 || seconds > 30) return ESP_ERR_INVALID_ARG;
    const char *name = strrchr(prefix, '/') + 1;
    for (const char *p = name; *p; ++p)
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') return ESP_ERR_INVALID_ARG;
    return admit(prefix, true, false, seconds, true);
}
esp_err_t yoke_voice_stop(void) {
    if (!player.control)
        return ESP_ERR_INVALID_STATE;
    lock();
    if (player.state != YOKE_VOICE_IDLE)
        player.stop = true;
    unlock();
    return ESP_OK;
}
esp_err_t yoke_voice_record_stop(void) {
    if (!player.control)
        return ESP_ERR_INVALID_STATE;
    lock();
    esp_err_t result = ESP_OK;
    if (player.state != YOKE_VOICE_IDLE && !player.record)
        result = ESP_ERR_INVALID_STATE;
    else if (player.state != YOKE_VOICE_IDLE)
        player.stop = true;
    unlock();
    return result;
}
esp_err_t yoke_voice_volume(unsigned percent) {
    if (!player.control)
        return ESP_ERR_INVALID_STATE;
    return yoke_voice_device_volume(player.config.device, percent);
}
esp_err_t yoke_voice_status(yoke_voice_status_t *s) {
    if (!s)
        return ESP_ERR_INVALID_ARG;
    if (!player.control)
        return ESP_ERR_INVALID_STATE;
    lock();
    *s = (yoke_voice_status_t){player.state, player.last_result, player.bytes, false};
    unlock();
    s->recording_quiescent = yoke_voice_device_quiet(player.config.device);
    return ESP_OK;
}
