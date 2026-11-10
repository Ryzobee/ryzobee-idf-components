#pragma once
#include "yoke_voice_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif
/* HTTP preflight, available with CONFIG_YOKE_VOICE_HTTP, independent of helper.
 * Checks an up default interface with an IPv4 address; no internet probe.
 * Returns ESP_ERR_INVALID_STATE and logs No network if unavailable.
 * Application owns network setup, HTTPS time and interface lifetime.
 */
esp_err_t yoke_voice_network_check(void);

/* Optional helper owns the default Wi-Fi STA. Configure before any session.
 * Do not use alongside an application-owned Wi-Fi/SNTP manager.
 * Connect requires configuration. For application-managed networking and TLS
 * time, disable CONFIG_YOKE_VOICE_NETWORK_HELPER; HTTP IO remains available.
 */
typedef struct { const char *ssid; const char *password; unsigned timeout_ms; } yoke_voice_network_config_t;
esp_err_t yoke_voice_network_configure(const yoke_voice_network_config_t *cfg);
esp_err_t yoke_voice_network_connect(const yoke_voice_control_t *control, bool tls);
void yoke_voice_network_disconnect(void);

#ifdef __cplusplus
}
#endif
