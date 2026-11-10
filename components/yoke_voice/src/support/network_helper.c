/* SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: Apache-2.0 */
#include <stdlib.h>
#include <string.h>
#include "yoke_voice_runtime.h"
#include "esp_gmf_io.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"
#include <time.h>
#include "yoke_voice_network.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
static inline bool player_gmf_cancelled(const yoke_voice_control_t *control) {
    return control && control->cancelled && control->cancelled(control->ctx);
}
#define CONNECTED BIT0
#define DISCONNECTED BIT1
static EventGroupHandle_t events;
static esp_netif_t *netif;
static esp_event_handler_instance_t wifi_handler, ip_handler;
static bool started, sntp_started;

static char network_ssid[33], network_password[65];
static unsigned network_timeout_ms = 15000;
static bool configured;
esp_err_t yoke_voice_network_configure(const yoke_voice_network_config_t *cfg) {
    if (!cfg || !cfg->ssid || !cfg->password || !cfg->ssid[0] ||
        strlen(cfg->ssid) > 32 || strlen(cfg->password) > 64 ||
        cfg->timeout_ms < 1000 || cfg->timeout_ms > 60000) return ESP_ERR_INVALID_ARG;
    if (netif) return ESP_ERR_INVALID_STATE;
    strcpy(network_ssid, cfg->ssid); strcpy(network_password, cfg->password);
    network_timeout_ms = cfg->timeout_ms; configured = true;
    return ESP_OK;
}
static void network_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupClearBits(events, DISCONNECTED);
        xEventGroupSetBits(events, CONNECTED);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(events, CONNECTED);
        xEventGroupSetBits(events, DISCONNECTED);
    }
}
/* 应用启动时初始化；Wi-Fi 在播放会话之间保持连接。
 * 不自动擦除 NVS：损坏或版本冲突作为错误交给应用处理。 */
static esp_err_t network_init(void) {
    if (netif)
        return ESP_OK;
    esp_err_t ret = nvs_flash_init();
    if (ret != ESP_OK)
        return ret;
    ret = esp_netif_init();
    if (ret != ESP_OK)
        return ret;
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
        return ret;
    events = xEventGroupCreate();
    if (!events)
        return ESP_ERR_NO_MEM;
    esp_netif_t *created = esp_netif_create_default_wifi_sta();
    if (!created) {
        vEventGroupDelete(events); events = NULL;
        return ESP_ERR_NO_MEM;
    }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&init);
    if (ret != ESP_OK)
        goto fail_netif;
    ret = esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, network_event, NULL, &wifi_handler);
    if (ret != ESP_OK)
        goto fail_wifi;
    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, network_event, NULL, &ip_handler);
    if (ret != ESP_OK)
        goto fail_handler;
    ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (ret == ESP_OK)
        ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler);
        goto fail_handler;
    }
    netif = created;
    return ESP_OK;
fail_handler:
    esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_handler);
fail_wifi:
    esp_wifi_deinit();
fail_netif:
    esp_netif_destroy_default_wifi(created);
    vEventGroupDelete(events); events = NULL;
    return ret;
}
esp_err_t yoke_voice_network_connect(const yoke_voice_control_t *control, bool tls) {
    if (player_gmf_cancelled(control))
        return ESP_ERR_INVALID_STATE;
    if (!configured) return ESP_ERR_INVALID_STATE;
    if (!network_ssid[0]) {
        ESP_LOGE("gmf_ci1302_network", "Wi-Fi SSID is empty. Call yoke_voice_network_configure before connecting.");
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t ret = network_init();
    if (ret != ESP_OK)
        return ret;
    int64_t end;
    if (!(xEventGroupGetBits(events) & CONNECTED)) {
        if (!started) {
            wifi_config_t cfg = {0};
            if (strlen(network_ssid) > sizeof(cfg.sta.ssid) ||
                strlen(network_password) > sizeof(cfg.sta.password)) {
                ESP_LOGE("gmf_ci1302_network", "Wi-Fi configuration is too long: SSID must be <= 32 bytes and password <= 64 bytes.");
                return ESP_ERR_INVALID_ARG;
            }
            memcpy(cfg.sta.ssid, network_ssid, strlen(network_ssid));
            memcpy(cfg.sta.password, network_password, strlen(network_password));
            ret = esp_wifi_set_config(WIFI_IF_STA, &cfg);
            if (ret != ESP_OK)
                return ret;
            ret = esp_wifi_start();
            if (ret != ESP_OK)
                return ret;
            started = true;
        }
        xEventGroupClearBits(events, CONNECTED | DISCONNECTED);
        ret = esp_wifi_connect();
        if (ret != ESP_OK)
            return ret;
        end = esp_timer_get_time() + network_timeout_ms * 1000LL;
        for (;;) {
            if (player_gmf_cancelled(control))
                return ESP_ERR_INVALID_STATE;
            EventBits_t bits = xEventGroupWaitBits(events, CONNECTED | DISCONNECTED, pdFALSE, pdFALSE, pdMS_TO_TICKS(20));
            if (bits & CONNECTED)
                break;
            if (bits & DISCONNECTED)
                return ESP_FAIL;
            if (esp_timer_get_time() >= end)
                return ESP_ERR_TIMEOUT;
        }
    }
    // HTTPS 校验证书有效期需要正确时间；不通过关闭证书验证规避。
    if (tls && time(NULL) < 1704067200) {
        if (!sntp_started) {
            esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            ret = esp_netif_sntp_init(&sntp);
            if (ret != ESP_OK)
                return ret;
            sntp_started = true;
        }
        end = esp_timer_get_time() + network_timeout_ms * 1000LL;
        while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(20)) != ESP_OK) {
            if (player_gmf_cancelled(control))
                return ESP_ERR_INVALID_STATE;
            if (esp_timer_get_time() >= end)
                return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}
void yoke_voice_network_disconnect(void) {
    if (sntp_started) {
        esp_netif_sntp_deinit();
        sntp_started = false;
    }
    if (started) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        started = false;
    }
}
