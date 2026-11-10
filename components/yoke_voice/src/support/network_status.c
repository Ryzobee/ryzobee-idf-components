#include "yoke_voice_network.h"
#include "esp_netif.h"
#include "esp_log.h"

esp_err_t yoke_voice_network_check(void) {
    /* No initialization, connection, retry, SNTP or remote reachability probe.
     * get_default_netif is safe even before esp_netif_init in IDF 6.0.2.
     * Applications must serialize interface destruction against playback.
     */
    esp_netif_t *netif = esp_netif_get_default_netif();
    esp_netif_ip_info_t ip = {0};
    if (!netif || !esp_netif_is_netif_up(netif) ||
        esp_netif_get_ip_info(netif, &ip) != ESP_OK || !ip.ip.addr) {
        ESP_LOGE("gmf_ci1302_http", "没有网络：请先由应用建立网络连接 (No network)");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}
