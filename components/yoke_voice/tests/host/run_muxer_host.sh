#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# 文件层使用标准 POSIX IO；只替换 ESP 错误码和 GMF 接口类型，不替换被测实现。
cat > "$work/esp_err.h" <<'HEADER'
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
HEADER
cat > "$work/esp_gmf_io.h" <<'HEADER'
#pragma once
typedef int esp_gmf_err_t;
typedef int esp_gmf_io_dir_t;
typedef void *esp_gmf_io_handle_t;
HEADER
cat > "$work/esp_gmf_pool.h" <<'HEADER'
#pragma once
typedef void *esp_gmf_pool_handle_t;
HEADER
"${CC:-cc}" -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
    -fsanitize=address,undefined -g -I"$work" -Iinclude \
    -Iinclude \
    src/support/record_files.c tests/host/test_muxer_files.c -o "$work/muxer"
ASAN_OPTIONS=detect_leaks=0 "$work/muxer" "$work"
