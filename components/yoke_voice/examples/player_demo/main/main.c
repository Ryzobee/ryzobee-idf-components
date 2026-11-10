/* SPDX-License-Identifier: CC0-1.0 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "yoke_voice_io.h"
#include "sdkconfig.h"
#include "yoke_voice_player.h"
#include "yoke_voice_network.h"

/* Edit these macros before building. Credentials are never logged. */
#ifndef EXAMPLE_WIFI_SSID
#define EXAMPLE_WIFI_SSID ""
#endif
#ifndef EXAMPLE_WIFI_PASSWORD
#define EXAMPLE_WIFI_PASSWORD ""
#endif
#ifndef EXAMPLE_WIFI_TIMEOUT_MS
#define EXAMPLE_WIFI_TIMEOUT_MS 15000
#endif

static esp_err_t example_network_connect(void) {
    if (!EXAMPLE_WIFI_SSID[0]) {
        ESP_LOGE("example_network", "未预设网络：请填写 main.c 中的 EXAMPLE_WIFI_SSID 和 EXAMPLE_WIFI_PASSWORD");
        return ESP_ERR_INVALID_ARG;
    }
    const yoke_voice_network_config_t config = {
        EXAMPLE_WIFI_SSID, EXAMPLE_WIFI_PASSWORD, EXAMPLE_WIFI_TIMEOUT_MS
    };
    esp_err_t ret = yoke_voice_network_configure(&config);
    if (ret != ESP_OK) return ret;
    return yoke_voice_network_connect(NULL, true);
}

static esp_err_t resources, recordings, hardware, audio;
static char latest_recording[40];
static unsigned next_recording = 1, next_muxer_recording = 1;
static unsigned volume = 100;

static esp_err_t mount(const char *path, const char *label) {
    const esp_vfs_littlefs_conf_t conf = {
        .base_path = path,
        .partition_label = label,
        .format_if_mount_failed = false,
    };
    ESP_LOGI("main", "Mounting partition %s at %s", label, path);
    esp_err_t result = esp_vfs_littlefs_register(&conf);
    if (result == ESP_OK)
        ESP_LOGI("main", "Partition %s mounted", label);
    else
        ESP_LOGE("main", "Partition %s mount failed: %s", label, esp_err_to_name(result));
    return result;
}

static void report(esp_err_t error) {
    printf("%s\n", esp_err_to_name(error));
}

static void audio_event(bool recording, esp_err_t error, const char *path) {
    printf("\n%s %s: %s\n", recording ? "record" : "play", path, esp_err_to_name(error));
}

static void record_next(void) {
    if (recordings != ESP_OK) {
        report(recordings);
        return;
    }
    char name[24];
    char path[sizeof(latest_recording)];
    while (next_recording <= 9999) {
        snprintf(name, sizeof(name), "rec_%04u.wav", next_recording++);
        snprintf(path, sizeof(path), "/recordings/%s", name);
        FILE *existing = fopen(path, "rb");
        if (existing) {
            fclose(existing);
            continue;
        }
        esp_err_t error = yoke_voice_record_start(path, 10);
        if (error == ESP_OK)
            strcpy(latest_recording, path);
        report(error);
        return;
    }
    printf("No recording names left\n");
}

static void record_muxer_next(void) {
    if (recordings != ESP_OK) {
        report(recordings);
        return;
    }
    char name[24], first[sizeof(latest_recording)], prefix[40];
    while (next_muxer_recording <= 9999) {
        snprintf(name, sizeof(name), "mux_%04u", next_muxer_recording++);
        snprintf(first, sizeof(first), "/recordings/%.16s_000.wav", name);
        FILE *existing = fopen(first, "rb");
        if (existing) {
            fclose(existing);
            continue;
        }
        snprintf(prefix, sizeof(prefix), "/recordings/%s", name);
        esp_err_t error = yoke_voice_record_muxer_start(prefix, 10);
        if (error == ESP_OK)
            strcpy(latest_recording, first);
        report(error);
        return;
    }
    printf("No muxer recording names left\n");
}

static void format_recordings(void) {
    yoke_voice_status_t status;
    if (yoke_voice_status(&status) == ESP_OK && status.state != YOKE_VOICE_IDLE) {
        report(ESP_ERR_INVALID_STATE);
        return;
    }
    if (recordings == ESP_OK)
        esp_vfs_littlefs_unregister("recordings");
    recordings = esp_littlefs_format("recordings");
    if (recordings == ESP_OK) {
        recordings = mount("/recordings", "recordings");
        latest_recording[0] = '\0';
        next_recording = 1;
        next_muxer_recording = 1;
    }
    report(recordings);
}

static void print_status(void) {
    printf("resources=%s recordings=%s CI1302=%s audio=%s heap=%zu\n",
           esp_err_to_name(resources), esp_err_to_name(recordings),
           esp_err_to_name(hardware), esp_err_to_name(audio),
           heap_caps_get_free_size(MALLOC_CAP_8BIT));
    yoke_voice_status_t status;
    if (yoke_voice_status(&status) == ESP_OK)
        printf("state=%d last=%s pcm_bytes=%lu\n", status.state,
               esp_err_to_name(status.last_result), (unsigned long)status.pcm_bytes);
}

static void change_volume(int delta) {
    if (delta > 0)
        volume = volume < 91 ? volume + 10 : 100;
    else
        volume = volume > 10 ? volume - 10 : 1;
    report(yoke_voice_volume(volume));
    printf("Volume: %u%%\n", volume);
}

static esp_err_t check_recording_space(void *ctx, size_t required) {
    (void)ctx;
    size_t total, used;
    esp_err_t ret = esp_littlefs_info("recordings", &total, &used);
    if (ret != ESP_OK) return ret;
    return used <= total && total - used >= required ? ESP_OK : ESP_ERR_NO_MEM;
}

void app_main(void) {
    resources = mount("/littlefs", "res");
    recordings = mount("/recordings", "recordings");
    yoke_voice_device_cfg_t cfg = YOKE_VOICE_DEVICE_CFG_DEFAULT();
    yoke_voice_device_handle_t device = NULL;
    hardware = yoke_voice_device_init(&cfg, &device);
    yoke_voice_player_config_t player_config = YOKE_VOICE_PLAYER_CONFIG_DEFAULT(device);
    player_config.callback = audio_event;
    player_config.check_space = check_recording_space;
    player_config.muxer_slice_ms = CONFIG_ROOTMAKER_MUXER_SLICE_MS;
    audio = hardware == ESP_OK ? yoke_voice_player_init(&player_config) : hardware;
    if (audio == ESP_OK)
        yoke_voice_volume(100);

    esp_err_t network = example_network_connect();
    if (network != ESP_OK)
        ESP_LOGE("main", "Network initialization failed: %s", esp_err_to_name(network));

    printf("Keys: 1-4 play MP3, h HTTP MP3, r record 10 s, m muxer WAV 10 s, s stop, p play recording, +/- volume, ? status, F then Y erase recordings\n");
    bool confirm_format = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));

        char input = '\0';
        if (scanf("%c", &input) != 1)
            continue;
        if (confirm_format) {
            if (input == '\r' || input == '\n')
                continue;
            confirm_format = false;
            if (input == 'Y') {
                format_recordings();
                continue;
            }
            printf("Erase cancelled\n");
        }
        if (input == '1') {
            report(yoke_voice_play_file("/littlefs/cmd_1.mp3"));
        } else if (input == '2') {
            report(yoke_voice_play_file("/littlefs/cmd_2.mp3"));
        } else if (input == '3') {
            report(yoke_voice_play_file("/littlefs/cmd_3.mp3"));
        } else if (input == '4') {
            report(yoke_voice_play_file("/littlefs/cmd_4.mp3"));
        } else if (input == 'h') {
            report(yoke_voice_play_http(CONFIG_ROOTMAKER_HTTP_URL));
        } else if (input == 'r') {
            record_next();
        } else if (input == 'm') {
            record_muxer_next();
        } else if (input == 's') {
            report(yoke_voice_stop());
        } else if (input == 'p') {
            report(latest_recording[0] ? yoke_voice_play_file(latest_recording) : ESP_ERR_NOT_FOUND);
        } else if (input == '+' || input == '-') {
            change_volume(input == '+' ? 10 : -10);
        } else if (input == '?') {
            print_status();
        } else if (input == 'F') {
            printf("Press Y to erase all recordings\n");
            confirm_format = true;
        } else if (input != '\r' && input != '\n') {
            printf("Unknown key: %c\n", input);
        }
    }
}
