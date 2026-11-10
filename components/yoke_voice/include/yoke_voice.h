#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
/* 设备在应用生命周期内保持有效；底层 CI1302 API 仅支持单设备。 */
typedef struct ci1302_device *yoke_voice_device_handle_t;
typedef struct {
    int uart_num, tx_pin, rx_pin;
    uint32_t baud_rate;
    unsigned startup_timeout_ms;
} yoke_voice_device_cfg_t;
#define YOKE_VOICE_DEVICE_CFG_DEFAULT() {1, 37, 36, 921600, 10000}

/* 初始化硬件并安装持久接收回调。重复初始化必须使用同一设备配置。 */
esp_err_t yoke_voice_device_init(const yoke_voice_device_cfg_t *cfg, yoke_voice_device_handle_t *out);
esp_err_t yoke_voice_device_volume(yoke_voice_device_handle_t dev, unsigned percent);
bool yoke_voice_device_quiet(yoke_voice_device_handle_t dev);
esp_err_t yoke_voice_device_wait_quiet(yoke_voice_device_handle_t dev, unsigned timeout_ms);


#ifdef __cplusplus
}
#endif
