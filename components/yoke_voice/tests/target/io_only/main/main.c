#include <assert.h>
#include "yoke_voice.h"
#include "yoke_voice_io.h"
void verify_cpp_linkage(void);
void app_main(void) {
    yoke_voice_device_cfg_t cfg = YOKE_VOICE_DEVICE_CFG_DEFAULT();
    yoke_voice_device_handle_t dev = NULL;
    cfg.uart_num = -1;
    assert(yoke_voice_device_init(&cfg, &dev) == ESP_ERR_INVALID_ARG);
    cfg.uart_num = 1; cfg.baud_rate = 0;
    assert(yoke_voice_device_init(&cfg, &dev) == ESP_ERR_INVALID_ARG);
    cfg.baud_rate = 921600; cfg.tx_pin = -1;
    assert(yoke_voice_device_init(&cfg, &dev) == ESP_ERR_INVALID_ARG);
    esp_gmf_io_handle_t io = NULL;
    yoke_voice_io_cfg_t invalid = YOKE_VOICE_IO_CFG_DEFAULT(NULL, ESP_GMF_IO_DIR_WRITER);
    assert(yoke_voice_io_init(&invalid, &io) == ESP_GMF_ERR_INVALID_ARG);
    assert(yoke_voice_device_volume(NULL, 100) == ESP_ERR_INVALID_STATE);
    verify_cpp_linkage();
}
