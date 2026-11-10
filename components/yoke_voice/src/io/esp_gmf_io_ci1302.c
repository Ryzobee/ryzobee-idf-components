#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "ci1302.h"
#include "yoke_voice_io.h"

#include "gmf_ci1302_device_internal.h"
typedef struct {
    esp_gmf_io_t base;
    yoke_voice_io_cfg_t cfg;
    portMUX_TYPE lock;
    yoke_voice_io_status_t status;
    bool stopped, used;
    uint32_t rx_errors_start;
    SemaphoreHandle_t lifecycle; // 串行化 open / finish / close / reset。
} ci1302_io_t;

static void command(uint16_t cmd) {
    ci1302_protocol_write_bytes(cmd, NULL, 0, UART_VERSION, DEF_FILL);
}
static void set_error(ci1302_io_t *io, esp_err_t err) {
    portENTER_CRITICAL(&io->lock);
    if (!io->status.error)
        io->status.error = err;
    portEXIT_CRITICAL(&io->lock);
}
esp_err_t yoke_voice_io_status(esp_gmf_io_handle_t handle, yoke_voice_io_status_t *out) {
    if (!handle || !out)
        return ESP_ERR_INVALID_ARG;
    ci1302_io_t *io = handle;
    portENTER_CRITICAL(&io->lock);
    *out = io->status;
    bool used = io->used;
    uint32_t rx_errors_start = io->rx_errors_start;
    portEXIT_CRITICAL(&io->lock);
    if (io->cfg.dir == ESP_GMF_IO_DIR_READER && out->opened) {
        struct ci1302_device *dev = io->cfg.device;
        portENTER_CRITICAL(&dev->lock);
        out->accepted_frames = dev->accepted_frames;
        if (!out->error)
            out->error = dev->rx_error;
        out->eof = dev->eof;
        portEXIT_CRITICAL(&dev->lock);
    }
    if (io->cfg.dir == ESP_GMF_IO_DIR_READER && used && !out->error &&
        ci1302_protocol_get_rx_error_count() != rx_errors_start)
        out->error = ESP_FAIL;
    return ESP_OK;
}
static bool cancelled(ci1302_io_t *io) {
    yoke_voice_io_status_t status;
    yoke_voice_io_status(io, &status);
    return status.cancelled || status.error;
}
static void finish_locked(ci1302_io_t *io) {
    if (io->cfg.dir != ESP_GMF_IO_DIR_READER || !io->status.opened || io->stopped)
        return;
    struct ci1302_device *dev = io->cfg.device;
    portENTER_CRITICAL(&dev->lock);
    dev->accepting = false;
    dev->eof = true;
    portEXIT_CRITICAL(&dev->lock);
    io->stopped = true;
    command(SET_AUDIO_STOP_RECORD);
}
void yoke_voice_io_finish(esp_gmf_io_handle_t handle) {
    if (!handle)
        return;
    ci1302_io_t *io = handle;
    xSemaphoreTake(io->lifecycle, portMAX_DELAY);
    finish_locked(io);
    xSemaphoreGive(io->lifecycle);
}
void yoke_voice_io_cancel(esp_gmf_io_handle_t handle) {
    if (!handle)
        return;
    ci1302_io_t *io = handle;
    portENTER_CRITICAL(&io->lock);
    io->status.cancelled = true;
    portEXIT_CRITICAL(&io->lock);
}
static esp_gmf_err_t open_locked(esp_gmf_io_handle_t handle) {
    ci1302_io_t *io = handle;
    struct ci1302_device *dev = io->cfg.device;
    if (!dev || !dev->ready || cancelled(io))
        return ESP_GMF_ERR_INVALID_STATE;
    portENTER_CRITICAL(&dev->lock);
    bool busy = dev->owner != NULL;
    if (!busy)
        dev->owner = io;
    portEXIT_CRITICAL(&dev->lock);
    if (busy) {
        set_error(io, ESP_ERR_INVALID_STATE);
        return ESP_GMF_ERR_INVALID_STATE;
    }
    if (io->cfg.dir == ESP_GMF_IO_DIR_READER && !yoke_voice_device_quiet(dev)) {
        portENTER_CRITICAL(&dev->lock);
        dev->owner = NULL;
        portEXIT_CRITICAL(&dev->lock);
        set_error(io, ESP_ERR_INVALID_STATE);
        return ESP_GMF_ERR_INVALID_STATE;
    }
    if (io->cfg.dir == ESP_GMF_IO_DIR_READER) {
        xQueueReset(dev->frames);
        portENTER_CRITICAL(&dev->lock);
        dev->accepted_frames = 0;
        dev->rx_error = ESP_OK;
        dev->eof = false;
        dev->quiet = false;
        dev->last_frame_us = esp_timer_get_time();
        dev->rx_errors_start = ci1302_protocol_get_rx_error_count();
        dev->accepting = true;
        portEXIT_CRITICAL(&dev->lock);
        command(SET_AUDIO_START_RECORD);
    } else
        ci1302_audio_write_start();
    // 配置和设备状态全部就绪后再发布 opened，避免控制任务读取半初始化状态。
    portENTER_CRITICAL(&io->lock);
    io->status.opened = true;
    io->used = true;
    io->rx_errors_start = dev->rx_errors_start;
    portEXIT_CRITICAL(&io->lock);
    return ESP_GMF_ERR_OK;
}
static esp_gmf_err_t io_open(esp_gmf_io_handle_t handle) {
    ci1302_io_t *io = handle;
    xSemaphoreTake(io->lifecycle, portMAX_DELAY);
    esp_gmf_err_t ret = io->status.opened ? ESP_GMF_ERR_OK : open_locked(handle);
    xSemaphoreGive(io->lifecycle);
    return ret;
}
static esp_gmf_err_t close_locked(esp_gmf_io_handle_t handle) {
    ci1302_io_t *io = handle;
    yoke_voice_io_status_t status;
    yoke_voice_io_status(io, &status);
    if (!status.opened)
        return ESP_GMF_ERR_OK;
    if (io->cfg.dir == ESP_GMF_IO_DIR_READER) {
        finish_locked(io);
        /* 快照留在实例，避免下一次设备会话覆盖其统计。 */
        portENTER_CRITICAL(&io->lock);
        io->status.accepted_frames = status.accepted_frames;
        if (!io->status.error)
            io->status.error = status.error;
        io->status.eof = status.eof;
        portEXIT_CRITICAL(&io->lock);
    } else if (!io->stopped) {
        if (status.eof && !status.cancelled && !status.error)
            ci1302_audio_write_stop();
        else
            command(NET_PLAY_STOP);
        io->stopped = true;
    }
    struct ci1302_device *dev = io->cfg.device;
    portENTER_CRITICAL(&dev->lock);
    if (dev->owner == io)
        dev->owner = NULL;
    portEXIT_CRITICAL(&dev->lock);
    portENTER_CRITICAL(&io->lock);
    io->status.opened = false;
    portEXIT_CRITICAL(&io->lock);
    return ESP_GMF_ERR_OK;
}
static esp_gmf_err_t io_close(esp_gmf_io_handle_t handle) {
    ci1302_io_t *io = handle;
    xSemaphoreTake(io->lifecycle, portMAX_DELAY);
    esp_gmf_err_t ret = close_locked(handle);
    xSemaphoreGive(io->lifecycle);
    return ret;
}
static esp_gmf_err_t io_reset(esp_gmf_io_handle_t handle) {
    ci1302_io_t *io = handle;
    xSemaphoreTake(io->lifecycle, portMAX_DELAY);
    close_locked(handle);
    portENTER_CRITICAL(&io->lock);
    memset(&io->status, 0, sizeof(io->status));
    io->stopped = false;
    io->used = false;
    portEXIT_CRITICAL(&io->lock);
    xSemaphoreGive(io->lifecycle);
    return ESP_GMF_ERR_OK;
}
static esp_gmf_err_io_t acquire_write(esp_gmf_io_handle_t io, void *payload, uint32_t n, int ticks) {
    (void)payload; (void)n; (void)ticks;
    return cancelled(io) ? ESP_GMF_IO_ABORT : ESP_GMF_IO_OK;
}
static esp_gmf_err_io_t write_pcm(esp_gmf_io_handle_t handle, void *payload, int ticks) {
    (void)ticks;
    ci1302_io_t *io = handle;
    esp_gmf_payload_t *p = payload;
    if (cancelled(io))
        return ESP_GMF_IO_ABORT;
    if ((p->valid_size & 1) || (p->valid_size && !p->buf)) {
        set_error(io, ESP_ERR_INVALID_SIZE);
        return ESP_GMF_IO_FAIL;
    }
    for (size_t offset = 0; offset < p->valid_size;) {
        size_t n = p->valid_size - offset;
        if (n > io->cfg.max_chunk)
            n = io->cfg.max_chunk;
        int64_t end = esp_timer_get_time() + io->cfg.flow_timeout_ms * 1000LL;
        while (!ci1302_flow_get_write_enable(n)) {
            if (cancelled(io))
                return ESP_GMF_IO_ABORT;
            if (!ci1302_audio_in_playing() || esp_timer_get_time() >= end) {
                set_error(io, ESP_ERR_TIMEOUT);
                return ESP_GMF_IO_FAIL;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (cancelled(io))
            return ESP_GMF_IO_ABORT;
        if (!ci1302_audio_in_playing()) {
            set_error(io, ESP_ERR_INVALID_STATE);
            return ESP_GMF_IO_FAIL;
        }
        /* 底层返回 void：统计提交量，不把它当作芯片的逐包确认。 */
        ci1302_write_audio_data(p->buf + offset, n);
        if (uart_wait_tx_done(io->cfg.device->cfg.uart_num, pdMS_TO_TICKS(io->cfg.tx_timeout_ms)) != ESP_OK) {
            set_error(io, ESP_ERR_TIMEOUT);
            return ESP_GMF_IO_FAIL;
        }
        offset += n;
        portENTER_CRITICAL(&io->lock);
        io->status.submitted_bytes += n;
        portEXIT_CRITICAL(&io->lock);
    }
    portENTER_CRITICAL(&io->lock);
    io->status.eof |= p->is_done;
    portEXIT_CRITICAL(&io->lock);
    return ESP_GMF_IO_OK;
}
static esp_gmf_err_io_t read_frame(esp_gmf_io_handle_t handle, void *payload, uint32_t wanted, int ticks) {
    (void)wanted; (void)ticks;
    ci1302_io_t *io = handle;
    struct ci1302_device *dev = io->cfg.device;
    esp_gmf_payload_t *p = payload;
    if (p->buf_length < YOKE_VOICE_RECORD_MAX_PACKET && esp_gmf_payload_realloc_buf(p, YOKE_VOICE_RECORD_MAX_PACKET) != ESP_GMF_ERR_OK)
        return ESP_GMF_IO_FAIL;
    for (;;) {
        if (cancelled(io))
            return ESP_GMF_IO_ABORT;
        yoke_voice_record_frame_t frame;
        if (xQueueReceive(dev->frames, &frame, pdMS_TO_TICKS(10)) == pdTRUE) {
            memcpy(p->buf, frame.data, frame.size);
            p->valid_size = frame.size;
            p->is_done = false;
            return ESP_GMF_IO_OK;
        }
        /* EOF 门控与最终出队同锁，确保停止前已经接收的包全部返回。 */
        portENTER_CRITICAL(&dev->lock);
        bool eof = dev->eof;
        bool queued = eof && xQueueReceive(dev->frames, &frame, 0) == pdTRUE;
        int64_t last = dev->last_frame_us;
        portEXIT_CRITICAL(&dev->lock);
        if (queued) {
            memcpy(p->buf, frame.data, frame.size);
            p->valid_size = frame.size;
            p->is_done = false;
            return ESP_GMF_IO_OK;
        }
        if (eof) {
            p->valid_size = 0;
            p->is_done = true;
            return ESP_GMF_IO_OK;
        }
        if (esp_timer_get_time() - last > io->cfg.rx_timeout_ms * 1000LL) {
            set_error(io, ESP_ERR_TIMEOUT);
            return ESP_GMF_IO_FAIL;
        }
    }
}
static esp_gmf_err_io_t release_read(esp_gmf_io_handle_t io, void *p, int ticks) {
    (void)io; (void)p; (void)ticks;
    return ESP_GMF_IO_OK;
}
static esp_gmf_err_t io_delete(esp_gmf_obj_handle_t obj) {
    io_close(obj);
    esp_gmf_io_deinit(obj);
    vSemaphoreDelete(((ci1302_io_t *)obj)->lifecycle);
    free(obj);
    return ESP_GMF_ERR_OK;
}
static esp_gmf_err_t io_new(void *cfg, esp_gmf_obj_handle_t *out) {
    return yoke_voice_io_init(cfg, (esp_gmf_io_handle_t *)out);
}
esp_gmf_err_t yoke_voice_io_init(const yoke_voice_io_cfg_t *cfg, esp_gmf_io_handle_t *out) {
    if (!cfg || !out || !cfg->device ||
        (cfg->dir != ESP_GMF_IO_DIR_READER && cfg->dir != ESP_GMF_IO_DIR_WRITER) ||
        !cfg->max_chunk || cfg->max_chunk > 1024 || (cfg->max_chunk & 1) ||
        !cfg->flow_timeout_ms || !cfg->tx_timeout_ms || !cfg->rx_timeout_ms)
        return ESP_GMF_ERR_INVALID_ARG;
    *out = NULL;
    ci1302_io_t *io = calloc(1, sizeof(*io));
    if (!io)
        return ESP_GMF_ERR_MEMORY_LACK;
    io->lifecycle = xSemaphoreCreateMutex();
    if (!io->lifecycle) {
        free(io);
        return ESP_GMF_ERR_MEMORY_LACK;
    }
    io->cfg = *cfg;
    io->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    io->base.parent.new_obj = io_new;
    io->base.parent.del_obj = io_delete;
    io->base.parent.cfg = &io->cfg;
    io->base.dir = cfg->dir;
    io->base.type = ESP_GMF_IO_TYPE_BYTE;
    io->base.open = io_open;
    io->base.close = io_close;
    io->base.reset = io_reset;
    if (cfg->dir == ESP_GMF_IO_DIR_READER) {
        io->base.acquire_read = read_frame;
        io->base.release_read = release_read;
    } else {
        io->base.acquire_write = acquire_write;
        io->base.release_write = write_pcm;
    }
    esp_gmf_io_cfg_t base_cfg = {0}; // 同步运行在流水线任务中。
    esp_gmf_err_t ret = esp_gmf_io_init(io, &base_cfg);
    if (ret != ESP_GMF_ERR_OK) {
        vSemaphoreDelete(io->lifecycle);
        free(io);
        return ret;
    }
    ret = esp_gmf_obj_set_tag(io, cfg->dir == ESP_GMF_IO_DIR_READER ? "io_ci1302_rx" : "io_ci1302_tx");
    if (ret != ESP_GMF_ERR_OK) {
        io_delete(io);
        return ret;
    }
    *out = io;
    return ESP_GMF_ERR_OK;
}
esp_gmf_err_t yoke_voice_io_register(esp_gmf_pool_handle_t pool, yoke_voice_device_handle_t dev) {
    for (int dir = ESP_GMF_IO_DIR_READER; dir <= ESP_GMF_IO_DIR_WRITER; ++dir) {
        yoke_voice_io_cfg_t cfg = YOKE_VOICE_IO_CFG_DEFAULT(dev, dir);
        esp_gmf_io_handle_t io = NULL;
        esp_gmf_err_t ret = yoke_voice_io_init(&cfg, &io);
        if (ret != ESP_GMF_ERR_OK)
            return ret;
        ret = esp_gmf_pool_register_io(pool, io, NULL);
        if (ret != ESP_GMF_ERR_OK) {
            esp_gmf_obj_delete(io);
            return ret;
        }
    }
    return ESP_GMF_ERR_OK;
}
