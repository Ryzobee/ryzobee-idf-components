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

static atomic_bool stop_requested, running;
static bool cancelled(void *ctx) { (void)ctx; return atomic_load(&stop_requested); }
static void progress(void *ctx, uint32_t bytes) { (void)ctx; (void)bytes; }
static void worker(void *arg) {
    yoke_voice_runtime_t *runtime = arg;
    yoke_voice_control_t control = {.cancelled=cancelled, .progress=progress};
    esp_err_t result;
    result = example_play_http(runtime, CONFIG_ROOTMAKER_HTTP_URL, &control);
    ESP_LOGI("example", "Result: %s", esp_err_to_name(result));
    atomic_store(&running, false);
    vTaskDelete(NULL);
}
void app_main(void) {
    esp_err_t network = example_network_connect();
    if (network != ESP_OK) {
        ESP_LOGE("example", "Network initialization failed: %s", esp_err_to_name(network));
        return;
    }
    yoke_voice_device_cfg_t cfg = YOKE_VOICE_DEVICE_CFG_DEFAULT();
    yoke_voice_device_handle_t device;
    ESP_ERROR_CHECK(yoke_voice_device_init(&cfg, &device));
    ESP_ERROR_CHECK(yoke_voice_device_volume(device, 100));
    yoke_voice_runtime_t *runtime;
    ESP_ERROR_CHECK(yoke_voice_runtime_create(device, &runtime));
    puts("Keys: h play HTTP, s stop. Network connects once during initialization.");
    for (;;) {
        int key = getchar();
        if (key == 's') {
            atomic_store(&stop_requested, true);
        } else if (key == 'h') {
            if (atomic_load(&running)) {
                ESP_LOGW("example", "Audio busy; stop or wait before playing");
            } else if (yoke_voice_network_check() == ESP_OK) {
                atomic_store(&stop_requested, false);
                atomic_store(&running, true);
                if (xTaskCreate(worker, "example", 8192, runtime, 12, NULL) != pdPASS) {
                    atomic_store(&running, false);
                    ESP_LOGE("example", "Cannot create example task");
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
