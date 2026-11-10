#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
# 主机测试仅需公共接口类型，包解析使用头文件中的真实实现。
cat > "$work/esp_err.h" <<'HEADER'
#pragma once
typedef int esp_err_t;
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
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
    -I"$work" -Iinclude tests/host/test_record.c -o "$work/record"
ASAN_OPTIONS=detect_leaks=0 "$work/record"
