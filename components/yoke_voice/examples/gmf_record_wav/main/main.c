/* SPDX-License-Identifier: CC0-1.0 */
#include <stdatomic.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "example.h"
#include "yoke_voice_network.h"
static atomic_bool stop_requested;
static void mount_fs(const char *path, const char *label) {
    esp_vfs_littlefs_conf_t cfg = {.base_path=path, .partition_label=label, .format_if_mount_failed=false};
    ESP_ERROR_CHECK(esp_vfs_littlefs_register(&cfg));
}
static bool cancelled(void *ctx) { (void)ctx; return atomic_load(&stop_requested); }
static uint32_t recorded_bytes;
static void progress(void *ctx, uint32_t bytes) { (void)ctx; recorded_bytes = bytes; }
static void worker(void *arg) {
    yoke_voice_runtime_t *runtime = arg;
    yoke_voice_control_t control = {.cancelled=cancelled, .progress=progress};
    esp_err_t result;
    const char *path = "/recordings/direct.wav";
    int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
    FILE *file = fd < 0 ? NULL : fdopen(fd, "w+b");
    if (!file) {
        if (fd >= 0) { close(fd); unlink(path); }
        result = ESP_FAIL;
    } else {
        uint8_t header[44]; uint32_t bytes = 0;
        yoke_voice_wav_header(header, 0);
        result = fwrite(header, 1, 44, file) == 44 ? ESP_OK : ESP_FAIL;
        if (result == ESP_OK) result = example_record(runtime, file, 10, &control);
        /* The finalized PCM byte count is supplied by the runner's progress callback. */
        bytes = recorded_bytes;
        yoke_voice_wav_header(header, bytes);
        if (fseek(file, 0, SEEK_SET) || fwrite(header, 1, 44, file) != 44 || fflush(file)) result = ESP_FAIL;
        if (fclose(file)) result = ESP_FAIL;
        if (result != ESP_OK) unlink(path);
    }
    ESP_LOGI("example", "Result: %s", esp_err_to_name(result));
    yoke_voice_runtime_destroy(runtime);
    vTaskDelete(NULL);
}
void app_main(void) {
    mount_fs("/recordings", "recordings");
    yoke_voice_device_cfg_t cfg = YOKE_VOICE_DEVICE_CFG_DEFAULT();
    yoke_voice_device_handle_t device;
    ESP_ERROR_CHECK(yoke_voice_device_init(&cfg, &device));
    yoke_voice_runtime_t *runtime;
    ESP_ERROR_CHECK(yoke_voice_runtime_create(device, &runtime));

    if (xTaskCreate(worker, "example", 8192, runtime, 12, NULL) != pdPASS) {
        yoke_voice_runtime_destroy(runtime);
        ESP_LOGE("example", "Cannot create example task"); return;
    }
    puts("Example runs once. Press s to stop; reset to run again.");
    for (;;) { int key = getchar(); if (key == 's') atomic_store(&stop_requested, true); vTaskDelay(pdMS_TO_TICKS(10)); }
}
