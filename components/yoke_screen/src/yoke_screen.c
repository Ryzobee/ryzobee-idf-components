/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Modified for RootMaker: board panel/touch setup, shared I2C lifecycle,
 * exclusive GPIO reservations, and recoverable cleanup.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_commands.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "lcd_panel_st7789_spec.h"
#include "rootmaker_pin.h"
#include "rootmaker_utils.h"

#define YOKE_SCREEN_LVGL_TASK_STACK_CAPS (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
/* esp_lvgl_port 2.x names its worker taskLVGL; it owns the singleton LVGL runtime. */
#define YOKE_SCREEN_LVGL_TASK_NAME "taskLVGL"
#define YOKE_SCREEN_STOP_TIMEOUT_MS 5000

#if CONFIG_SPIRAM && SOC_PSRAM_DMA_CAPABLE
#define YOKE_SCREEN_LVGL_DRAW_BUFFER_IN_EXTERNAL_MEMORY 1
#else
#define YOKE_SCREEN_LVGL_DRAW_BUFFER_IN_EXTERNAL_MEMORY 0
#endif

/* LCD size */
#define LCD_H_RES (240)
#define LCD_V_RES (240)

/* LCD settings */
#define LCD_SPI_NUM (SPI3_HOST)
#define LCD_PIXEL_CLK_HZ (80 * 1000 * 1000)
#define LCD_CMD_BITS (8)
#define LCD_PARAM_BITS (8)
#define LCD_BITS_PER_PIXEL (16)
#define LCD_DRAW_BUFF_DOUBLE (1)
#define LCD_BL_ON_LEVEL (1)

/* CST816T touch settings */
#define TOUCH_I2C_CLOCK_HZ 400000
#define TOUCH_I2C_ADDRESS 0x15
#define TOUCH_I2C_TIMEOUT_MS 20

/* LCD pins */
#define LCD_GPIO_SCLK (LCD_SCK)
#define LCD_GPIO_MOSI (LCD_SDA)
#define LCD_GPIO_RST (LCD_RST)
#define LCD_GPIO_DC (LCD_RS)
#define LCD_GPIO_CS (LCD_CS)
#define LCD_GPIO_BL (LCD_BL)

static const char *TAG = "SCREEN";

static const st7789_lcd_init_cmd_t st7789_boe154_init[] = {
    // { cmd, data, data_size, delay_ms }

    { 0x11, NULL,                                                                                              0,  120 }, // Sleep Out

    { 0x36, (uint8_t[]){ 0x00 },                                                                               1,  0   }, // MADCTL
    { 0x3A, (uint8_t[]){ 0x05 },                                                                               1,  0   }, // COLMOD: 16bit
    { 0x21, NULL,                                                                                              0,  0   }, // Display inversion ON

    // CASET
    { 0x2A, (uint8_t[]){ 0x00, 0x00, 0x00, 0xEF },                                                             4,  0   },
    // RASET
    { 0x2B, (uint8_t[]){ 0x00, 0x00, 0x00, 0xEF },                                                             4,  0   },

    // Porch
    { 0xB2, (uint8_t[]){ 0x0C, 0x0C, 0x00, 0x33, 0x33 },                                                       5,  0   },
    // Gate control
    { 0xB7, (uint8_t[]){ 0x35 },                                                                               1,  0   },

    // Power & VCOM
    { 0xBB, (uint8_t[]){ 0x1F },                                                                               1,  0   },
    { 0xC0, (uint8_t[]){ 0x2C },                                                                               1,  0   },
    { 0xC2, (uint8_t[]){ 0x01 },                                                                               1,  0   },
    { 0xC3, (uint8_t[]){ 0x12 },                                                                               1,  0   },
    { 0xC4, (uint8_t[]){ 0x20 },                                                                               1,  0   },
    { 0xC6, (uint8_t[]){ 0x0F },                                                                               1,  0   },
    { 0xD0, (uint8_t[]){ 0xA4, 0xA1 },                                                                         2,  0   },

    // Positive gamma
    { 0xE0, (uint8_t[]){ 0xD0, 0x08, 0x11, 0x08, 0x0C, 0x15, 0x39, 0x33, 0x50, 0x36, 0x13, 0x14, 0x29, 0x2D }, 14, 0   },

    // Negative gamma
    { 0xE1, (uint8_t[]){ 0xD0, 0x08, 0x10, 0x08, 0x06, 0x06, 0x39, 0x44, 0x51, 0x0B, 0x16, 0x14, 0x2F, 0x31 }, 14, 0   },

    { 0x29, NULL,                                                                                              0,  0   }  // Display ON
};

static st7789_vendor_config_t st7789_boe154_vendor_config = {
    .init_cmds = st7789_boe154_init,
    .init_cmds_size = sizeof(st7789_boe154_init) / sizeof(st7789_boe154_init[0]),
};

/* LCD IO and panel */
static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;
static bool lcd_spi_initialized = false;
static bool lvgl_initialized = false;
static bool lvgl_stop_requested = false;
static bool screen_stopping = false;
static bool touch_reset_configured = false;
static bool backlight_configured = false;
static bool screen_gpio_held = false;
static gpio_num_t touch_pins[2];
/* Reserve all board display/touch signals except the shared SDA/SCL pair.
 * TP_INT is wired to the touch controller even though this driver polls it. */
static gpio_num_t screen_pins[] = {
    LCD_GPIO_SCLK, LCD_GPIO_MOSI, LCD_GPIO_RST, LCD_GPIO_DC, LCD_GPIO_CS, LCD_GPIO_BL, TP_RST, TP_INT,
};
static rootmaker_port_t screen_gpio_port = {
    .type = ROOTMAKER_PORT_TYPE_F_BUS,
    .pin_num = sizeof(screen_pins) / sizeof(screen_pins[0]),
    .pins = screen_pins,
};

/* LVGL display and touch */
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *touch_indev = NULL;
static i2c_master_bus_handle_t touch_i2c_bus = NULL;
static i2c_master_dev_handle_t touch_i2c_device = NULL;
static rootmaker_port_t touch_i2c_port;
static bool touch_i2c_bus_ref_held = false;
static bool touch_address_held = false;

static esp_err_t touch_init(void) {
    if (touch_i2c_device != NULL)
        return ESP_ERR_INVALID_STATE;

    const gpio_config_t reset_config = {
        .pin_bit_mask = 1ULL << TP_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&reset_config), TAG, "Touch reset GPIO configuration failed");
    touch_reset_configured = true;
    ESP_RETURN_ON_ERROR(gpio_set_level(TP_RST, 0), TAG, "Touch reset assert failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(gpio_set_level(TP_RST, 1), TAG, "Touch reset release failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_I2C_ADDRESS,
        .scl_speed_hz = TOUCH_I2C_CLOCK_HZ,
    };
    return i2c_master_bus_add_device(touch_i2c_bus, &device_config, &touch_i2c_device);
}

static bool touch_read(uint16_t *x, uint16_t *y) {
    if (touch_i2c_device == NULL || x == NULL || y == NULL)
        return false;

    uint8_t data[6] = { 0 };
    const uint8_t register_address = 0x01;
    if (i2c_master_transmit_receive(touch_i2c_device, &register_address, sizeof(register_address), data, sizeof(data), TOUCH_I2C_TIMEOUT_MS) != ESP_OK ||
        data[1] == 0) {
        return false;
    }

    uint16_t raw_x = ((uint16_t)(data[2] & 0x0f) << 8) | data[3];
    uint16_t raw_y = ((uint16_t)(data[4] & 0x0f) << 8) | data[5];
    if (raw_x >= LCD_H_RES || raw_y >= LCD_V_RES)
        return false;

    /* Board orientation: mirror both axes, matching the previous touch configuration. */
    *x = LCD_H_RES - raw_x - 1;
    *y = LCD_V_RES - raw_y - 1;
    return true;
}

static esp_err_t screen_cleanup(void);

static esp_err_t app_lcd_init(void) {
    esp_err_t ret = ESP_OK;

    /* LCD backlight */
    gpio_config_t bk_gpio_config = { .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << LCD_GPIO_BL };
    ESP_GOTO_ON_ERROR(gpio_config(&bk_gpio_config), err, TAG, "Backlight GPIO configuration failed");
    backlight_configured = true;
    ESP_GOTO_ON_ERROR(gpio_set_level(LCD_GPIO_BL, 0), err, TAG, "Backlight disable failed");

    /* LCD initialization */
    ESP_LOGD(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_GPIO_SCLK,
        .mosi_io_num = LCD_GPIO_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 64 * 1024,
    };
    ESP_GOTO_ON_ERROR(spi_bus_initialize(LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO), err, TAG, "SPI init failed");
    lcd_spi_initialized = true;

    ESP_LOGD(TAG, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_GPIO_DC,
        .cs_gpio_num = LCD_GPIO_CS,
        .pclk_hz = LCD_PIXEL_CLK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 3,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_NUM, &io_config, &lcd_io), err, TAG, "New panel IO failed");

    ESP_LOGD(TAG, "Install LCD driver");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_GPIO_RST,
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)
        .rgb_endian = LCD_RGB_ENDIAN_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#endif
        .bits_per_pixel = LCD_BITS_PER_PIXEL,
        .vendor_config = &st7789_boe154_vendor_config,
    };
    ESP_GOTO_ON_ERROR(lcd_new_panel_st7789_spec(lcd_io, &panel_config, &lcd_panel), err, TAG, "New panel failed");

    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(lcd_panel), err, TAG, "Panel reset failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(lcd_panel), err, TAG, "Panel initialization failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_set_gap(lcd_panel, 0, 320 - 240), err, TAG, "Panel gap setup failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_disp_on_off(lcd_panel, true), err, TAG, "Panel enable failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_swap_xy(lcd_panel, true), err, TAG, "Panel rotation setup failed");

    /* LCD backlight on */
    ESP_GOTO_ON_ERROR(gpio_set_level(LCD_GPIO_BL, LCD_BL_ON_LEVEL), err, TAG, "Backlight enable failed");

    return ret;

err:
    return ret;
}

static void lvgl_port_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;
    uint16_t x;
    uint16_t y;
    if (touch_read(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

static esp_err_t lvgl_tp_init(lv_display_t *disp) {
    ESP_RETURN_ON_ERROR(touch_init(), TAG, "Touch initialization failed");

    lvgl_port_lock(0);
    /* Register a touchpad input device */
    touch_indev = lv_indev_create();
    if (touch_indev == NULL) {
        lvgl_port_unlock();
        return ESP_ERR_NO_MEM;
    }
    lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_indev, lvgl_port_touchpad_read);
    lv_indev_set_disp(touch_indev, disp);
    lvgl_port_unlock();
    return ESP_OK;
}

static esp_err_t app_lvgl_init(void) {
    /* Do not overwrite a runtime owned by another user, or one still exiting. */
    if (xTaskGetHandle(YOKE_SCREEN_LVGL_TASK_NAME) != NULL || lv_is_initialized())
        return ESP_ERR_INVALID_STATE;
    /* Initialize LVGL */
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,       /* LVGL task priority */
        .task_stack = 10240,      /* LVGL task stack size */
        .task_affinity = -1,      /* LVGL task pinned to core (-1 is no affinity) */
        .task_max_sleep_ms = 500, /* Maximum sleep in LVGL task */
        .task_stack_caps = YOKE_SCREEN_LVGL_TASK_STACK_CAPS,
        .timer_period_ms = 5 /* LVGL timer tick period in ms */
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port initialization failed");
    lvgl_initialized = true;

    /* Add LCD screen */
    ESP_LOGD(TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_io,
        .panel_handle = lcd_panel,
        /* esp_lvgl_port takes pixels and applies the RGB565 byte size itself. */
        .buffer_size = LCD_H_RES * LCD_V_RES,
        .double_buffer = LCD_DRAW_BUFF_DOUBLE,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
#if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif
        .rotation = {
            .swap_xy = false,
            .mirror_x = true,
            .mirror_y = true,
        },
        .flags = {
            .buff_spiram = YOKE_SCREEN_LVGL_DRAW_BUFFER_IN_EXTERNAL_MEMORY,
            .buff_dma = true,
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = true,
#endif
        }
    };

    lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    if (lvgl_disp == NULL) {
        ESP_LOGE(TAG, "LVGL display buffer allocation failed");
        return ESP_ERR_NO_MEM;
    }
    esp_err_t ret = lvgl_tp_init(lvgl_disp);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

static esp_err_t screen_cleanup(void) {
    screen_stopping = true;
    esp_err_t ret;
    if (backlight_configured) {
        ret = gpio_set_level(LCD_GPIO_BL, 0);
        if (ret != ESP_OK)
            return ret;
    }
    if (touch_indev != NULL) {
        if (!lvgl_port_lock(0))
            return ESP_ERR_TIMEOUT;
        lv_indev_delete(touch_indev);
        touch_indev = NULL;
        lvgl_port_unlock();
    }
    if (lvgl_disp != NULL) {
        /* Hold the LVGL lock across draining and removal so no new flush can
         * start. A parameter transaction waits for queued SPI color transfers,
         * including their flush-ready callbacks, before returning. */
        if (!lvgl_port_lock(0))
            return ESP_ERR_TIMEOUT;
        ret = esp_lcd_panel_io_tx_param(lcd_io, -1, NULL, 0);
        if (ret != ESP_OK) {
            lvgl_port_unlock();
            return ret;
        }
        ret = lvgl_port_remove_disp(lvgl_disp);
        lvgl_port_unlock();
        if (ret != ESP_OK)
            return ret;
        lvgl_disp = NULL;
    }
    if (lvgl_initialized) {
        if (!lvgl_stop_requested) {
            /* Wake before requesting stop: an idle worker may sleep 500 ms. */
            (void)lvgl_port_task_wake(LVGL_PORT_EVENT_USER, NULL);
            ret = lvgl_port_deinit();
            if (ret != ESP_OK)
                return ret;
            lvgl_stop_requested = true;
        }
        const TickType_t stop_tick = xTaskGetTickCount();
        /* Look up the name afresh instead of dereferencing a deleted TCB. */
        while (xTaskGetHandle(YOKE_SCREEN_LVGL_TASK_NAME) != NULL) {
            if (xTaskGetTickCount() - stop_tick >= pdMS_TO_TICKS(YOKE_SCREEN_STOP_TIMEOUT_MS))
                return ESP_ERR_TIMEOUT;
            vTaskDelay(1);
        }
        lvgl_initialized = false;
        lvgl_stop_requested = false;
    }
    if (touch_i2c_device != NULL) {
        ret = i2c_master_bus_rm_device(touch_i2c_device);
        if (ret != ESP_OK)
            return ret;
        touch_i2c_device = NULL;
    }
    if (touch_address_held) {
        ret = rootmaker_release_i2c_address(touch_i2c_bus, TOUCH_I2C_ADDRESS);
        if (ret != ESP_OK)
            return ret;
        touch_address_held = false;
    }
    if (touch_i2c_bus_ref_held) {
        ret = rootmaker_del_i2c_bus(&touch_i2c_port);
        if (ret != ESP_OK)
            return ret;
        touch_i2c_bus_ref_held = false;
        touch_i2c_bus = NULL;
    }
    if (lcd_panel != NULL) {
        ret = esp_lcd_panel_disp_on_off(lcd_panel, false);
        if (ret != ESP_OK)
            return ret;
        ret = esp_lcd_panel_del(lcd_panel);
        if (ret != ESP_OK)
            return ret;
        lcd_panel = NULL;
    }
    if (lcd_io != NULL) {
        ret = esp_lcd_panel_io_del(lcd_io);
        if (ret != ESP_OK)
            return ret;
        lcd_io = NULL;
    }
    if (lcd_spi_initialized) {
        ret = spi_bus_free(LCD_SPI_NUM);
        if (ret != ESP_OK)
            return ret;
        lcd_spi_initialized = false;
    }
    if (touch_reset_configured) {
        ret = gpio_reset_pin(TP_RST);
        if (ret != ESP_OK)
            return ret;
        touch_reset_configured = false;
    }
    if (backlight_configured) {
        ret = gpio_reset_pin(LCD_GPIO_BL);
        if (ret != ESP_OK)
            return ret;
        backlight_configured = false;
    }
    if (screen_gpio_held) {
        ret = rootmaker_release_port(&screen_gpio_port);
        if (ret != ESP_OK)
            return ret;
        screen_gpio_held = false;
    }
    screen_stopping = false;
    return ESP_OK;
}

lv_display_t *yoke_screen_with_lvgl_init(rootmaker_port_t port) {
    if (screen_stopping || lcd_spi_initialized || lcd_io != NULL || lvgl_initialized || touch_i2c_bus_ref_held || lcd_panel != NULL || lvgl_disp != NULL ||
        touch_i2c_device != NULL || screen_gpio_held)
        return NULL;

    gpio_num_t sda, scl;
    if (rootmaker_get_port_pins(&port, &sda, &scl) != ESP_OK || sda == scl)
        return NULL;
    touch_pins[0] = sda;
    touch_pins[1] = scl;
    touch_i2c_port = (rootmaker_port_t){ .type = port.type, .pin_num = 2, .pins = touch_pins };

    /* Acquire resources before touching hardware. SDA/SCL remain shareable;
     * every other screen signal is exclusive until cleanup completes. */
    esp_err_t ret = rootmaker_use_port(&screen_gpio_port);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Screen GPIO reservation failed: %s", esp_err_to_name(ret));
        return NULL;
    }
    screen_gpio_held = true;
    ret = rootmaker_new_i2c_bus(&touch_i2c_port, &touch_i2c_bus);
    if (ret != ESP_OK)
        goto fail;
    touch_i2c_bus_ref_held = true;
    ret = rootmaker_claim_i2c_address(touch_i2c_bus, TOUCH_I2C_ADDRESS);
    if (ret != ESP_OK)
        goto fail;
    touch_address_held = true;

    /* LCD HW initialization */
    ret = app_lcd_init();
    if (ret != ESP_OK)
        goto fail;

    /* LVGL initialization */
    ret = app_lvgl_init();
    if (ret != ESP_OK)
        goto fail;

    return lvgl_disp;

fail:
    ESP_LOGE(TAG, "Screen initialization failed: %s", esp_err_to_name(ret));
    ret = screen_cleanup();
    if (ret != ESP_OK)
        ESP_LOGE(TAG, "Screen cleanup incomplete; retry yoke_screen_deinit(): %s", esp_err_to_name(ret));
    return NULL;
}

esp_err_t yoke_screen_deinit(void) {
    if (lcd_panel == NULL && lcd_io == NULL && !lcd_spi_initialized && lvgl_disp == NULL && touch_indev == NULL && touch_i2c_device == NULL &&
        !touch_i2c_bus_ref_held && !touch_address_held && !lvgl_initialized && !touch_reset_configured && !backlight_configured && !screen_gpio_held &&
        !screen_stopping) {
        return ESP_ERR_INVALID_STATE;
    }
    return screen_cleanup();
}
