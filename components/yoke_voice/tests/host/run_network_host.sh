#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/yoke_voice_network.h" <<'HEADER'
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
esp_err_t yoke_voice_network_check(void);
HEADER
cat > "$work/esp_netif.h" <<'HEADER'
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "yoke_voice_network.h"
typedef struct esp_netif { int unused; } esp_netif_t;
typedef struct { struct { uint32_t addr; } ip; } esp_netif_ip_info_t;
esp_netif_t *esp_netif_get_default_netif(void);
bool esp_netif_is_netif_up(esp_netif_t *netif);
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info);
HEADER
cat > "$work/esp_log.h" <<'HEADER'
#pragma once
void test_log(const char *message);
#define ESP_LOGE(tag, message) test_log(message)
HEADER
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -I"$work" src/support/network_status.c tests/host/test_network.c -o "$work/network"
ASAN_OPTIONS=detect_leaks=0 "$work/network"
