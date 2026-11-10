#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "esp_netif.h"
static esp_netif_t interface;
static bool exists, up;
static uint32_t address;
static esp_err_t ip_result;
static unsigned logs, ip_queries;
esp_netif_t *esp_netif_get_default_netif(void) { return exists ? &interface : NULL; }
bool esp_netif_is_netif_up(esp_netif_t *netif) { assert(netif == &interface); return up; }
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info) {
    assert(netif == &interface); ++ip_queries; info->ip.addr = address; return ip_result;
}
void test_log(const char *message) { assert(strstr(message, "No network")); ++logs; }
int main(void) {
    /* No netif also models a never-initialized network stack. */
    assert(yoke_voice_network_check() == ESP_ERR_INVALID_STATE);
    assert(ip_queries == 0 && logs == 1);
    exists = true;
    assert(yoke_voice_network_check() == ESP_ERR_INVALID_STATE);
    assert(ip_queries == 0 && logs == 2);
    up = true;
    assert(yoke_voice_network_check() == ESP_ERR_INVALID_STATE);
    address = 0x01020304;
    ip_result = -1;
    assert(yoke_voice_network_check() == ESP_ERR_INVALID_STATE);
    ip_result = ESP_OK;
    assert(yoke_voice_network_check() == ESP_OK);
    assert(logs == 4);
    up = false;
    assert(yoke_voice_network_check() == ESP_ERR_INVALID_STATE);
    puts("Network preflight: uninitialized, down, missing IP, query failure, ready and disconnect: PASS");
}
