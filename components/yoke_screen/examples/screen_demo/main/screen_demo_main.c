/*
 * Yoke-Screen touch demo: initialize the ST7789 display, CST816T touch
 * controller and LVGL, then visualize pointer events and coordinates.
 */

#include <inttypes.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "yoke_screen.h"

static const char *TAG = "screen_demo";

typedef struct {
    lv_obj_t *pad;
    lv_obj_t *dot;
    lv_obj_t *state_label;
    lv_obj_t *coord_label;
    uint32_t press_count;
} touch_demo_t;

static touch_demo_t s_demo;

static void set_dot_position(const lv_point_t *point) {
    lv_area_t pad_area;
    lv_obj_get_coords(s_demo.pad, &pad_area);

    const int32_t dot_radius = lv_obj_get_width(s_demo.dot) / 2;
    int32_t x = point->x - pad_area.x1 - dot_radius;
    int32_t y = point->y - pad_area.y1 - dot_radius;

    x = LV_CLAMP(0, x, lv_obj_get_width(s_demo.pad) - lv_obj_get_width(s_demo.dot));
    y = LV_CLAMP(0, y, lv_obj_get_height(s_demo.pad) - lv_obj_get_height(s_demo.dot));
    lv_obj_set_pos(s_demo.dot, x, y);
    lv_obj_remove_flag(s_demo.dot, LV_OBJ_FLAG_HIDDEN);
}

static void touch_event_cb(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev = lv_event_get_indev(event);
    lv_point_t point = { 0 };

    if (indev != NULL) {
        lv_indev_get_point(indev, &point);
    }

    switch (code) {
        case LV_EVENT_PRESSED:
            s_demo.press_count++;
            lv_label_set_text_fmt(s_demo.state_label, "PRESSED  #%" PRIu32, s_demo.press_count);
            lv_obj_set_style_bg_color(s_demo.pad, lv_palette_lighten(LV_PALETTE_BLUE, 4), 0);
            set_dot_position(&point);
            ESP_LOGI(TAG, "touch pressed: x=%" PRId32 ", y=%" PRId32, point.x, point.y);
            break;

        case LV_EVENT_PRESSING:
            lv_label_set_text(s_demo.state_label, "MOVING");
            set_dot_position(&point);
            break;

        case LV_EVENT_RELEASED:
            lv_label_set_text(s_demo.state_label, "RELEASED");
            lv_obj_set_style_bg_color(s_demo.pad, lv_color_hex(0xe9eef5), 0);
            set_dot_position(&point);
            ESP_LOGI(TAG, "touch released: x=%" PRId32 ", y=%" PRId32, point.x, point.y);
            break;

        default:
            return;
    }

    lv_label_set_text_fmt(s_demo.coord_label, "x: %3" PRId32 "   y: %3" PRId32, point.x, point.y);
}

static void create_touch_demo(void) {
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xf7f8fa), 0);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Yoke Touch Test");
    lv_obj_set_style_text_color(title, lv_color_hex(0x172033), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    s_demo.state_label = lv_label_create(screen);
    lv_label_set_text(s_demo.state_label, "Touch the area below");
    lv_obj_set_style_text_color(s_demo.state_label, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_align(s_demo.state_label, LV_ALIGN_TOP_MID, 0, 29);

    s_demo.pad = lv_obj_create(screen);
    lv_obj_set_size(s_demo.pad, 220, 154);
    lv_obj_align(s_demo.pad, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_radius(s_demo.pad, 10, 0);
    lv_obj_set_style_border_width(s_demo.pad, 2, 0);
    lv_obj_set_style_border_color(s_demo.pad, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_bg_color(s_demo.pad, lv_color_hex(0xe9eef5), 0);
    lv_obj_remove_flag(s_demo.pad, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_demo.pad, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_demo.pad, touch_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_demo.pad, touch_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_demo.pad, touch_event_cb, LV_EVENT_RELEASED, NULL);

    lv_obj_t *hint = lv_label_create(s_demo.pad);
    lv_label_set_text(hint, "PRESS / DRAG");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x8490a3), 0);
    lv_obj_center(hint);
    lv_obj_remove_flag(hint, LV_OBJ_FLAG_CLICKABLE);

    s_demo.dot = lv_obj_create(s_demo.pad);
    lv_obj_set_size(s_demo.dot, 18, 18);
    lv_obj_set_style_radius(s_demo.dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_demo.dot, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_border_width(s_demo.dot, 0, 0);
    lv_obj_set_style_pad_all(s_demo.dot, 0, 0);
    lv_obj_remove_flag(s_demo.dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_demo.dot, LV_OBJ_FLAG_HIDDEN);

    s_demo.coord_label = lv_label_create(screen);
    lv_label_set_text(s_demo.coord_label, "x: ---   y: ---");
    lv_obj_set_style_text_color(s_demo.coord_label, lv_color_hex(0x172033), 0);
    lv_obj_align(s_demo.coord_label, LV_ALIGN_BOTTOM_MID, 0, -10);
}

void app_main(void) {
    lv_display_t *display = yoke_screen_with_lvgl_init(rootmaker_sys_i2c_port);
    if (display == NULL) {
        ESP_LOGE(TAG, "screen or touch initialization failed");
        return;
    }

    lvgl_port_lock(0);
    create_touch_demo();
    lvgl_port_unlock();

    ESP_LOGI(TAG, "touch demo ready: press and drag inside the blue area");
}
