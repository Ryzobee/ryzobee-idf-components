# Yoke-Screen

Designed for the **RootMaker development board (ESP32-S3)**.

ESP-IDF driver for the RootMaker Yoke board display: a 240x240 ST7789 LCD on
SPI, a CST816T touch controller on the board system I2C bus, and an LVGL port wired to both. Board
pins and panel orientation are fixed by the hardware design.

## Installation

```yaml
dependencies:
  ryzobeezero/yoke_screen: "^1.0.0"
```

Run `idf.py reconfigure` (or `idf.py build`) to download it. The component
resolves `espressif/esp_lvgl_port` and `lvgl/lvgl` automatically.

## Usage

```c
#include "yoke_screen.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

lv_display_t *disp = yoke_screen_with_lvgl_init(rootmaker_sys_i2c_port);
if (disp == NULL) {
    ESP_LOGE("demo", "screen init failed");
    return;
}

// Create your UI from any task, wrapped in the LVGL port lock.
lvgl_port_lock(0);
lv_obj_t *label = lv_label_create(lv_screen_active());
lv_label_set_text(label, "Hello Yoke");
lv_obj_center(label);
lvgl_port_unlock();
```

The touch controller uses the I2C port passed to initialization; pass
`rootmaker_sys_i2c_port` for the board system bus. If
another component already acquired that bus, the screen driver reuses it.

Call `yoke_screen_deinit()` only after stopping application LVGL activity. It removes
the LVGL display and touch input, releases the touch device, and releases one
reference to the shared system I2C bus. The bus is deleted only when its final
reference is released. The application must not use the returned display handle after successful cleanup.
Reinitialization is allowed only after cleanup completes successfully.
Cleanup drains pending SPI transfers before deleting the LVGL display and waits
up to 5 seconds for the `taskLVGL` worker used by esp_lvgl_port 2.x to exit.
The component owns the global LVGL port; do not initialize another LVGL port or
create another task named `taskLVGL` alongside it. A timeout retains resources
for a later cleanup retry.

### Drawing with LVGL

Create labels, images, and canvases through LVGL. Application code must not send
pixel transfers directly to the panel while LVGL owns the display. The former
`yoke_screen_draw_bitmap()` API has been removed; migrate bitmaps to `lv_image`
or `lv_canvas`. LVGL manages display flushing and transfer completion.

For a writable bitmap, create a canvas after the display has initialized:

```c
LV_DRAW_BUF_DEFINE_STATIC(canvas_buffer, 64, 64, LV_COLOR_FORMAT_RGB565);

lvgl_port_lock(0);
LV_DRAW_BUF_INIT_STATIC(canvas_buffer);
lv_obj_t *canvas = lv_canvas_create(lv_display_get_screen_active(disp));
lv_canvas_set_draw_buf(canvas, &canvas_buffer);
lv_canvas_fill_bg(canvas, lv_color_hex(0x2070e0), LV_OPA_COVER);
lv_obj_center(canvas);
lvgl_port_unlock();
```

The static canvas buffer remains valid for the canvas lifetime. For dynamically
allocated image/canvas data, keep it allocated while LVGL can still reference it.
Use `lv_image_set_src()` for image resources and the same LVGL locking rule when
updating widgets from application tasks.

## Requirements

* ESP-IDF 5.3 or later
* RootMaker development board (ESP32-S3), with PSRAM enabled

## Example

See [`examples/screen_demo`](examples/screen_demo).

## Usage and API semantics

The ST7789 receives display commands and pixels through **SPI3**. The CST816T
uses **I2C** for touch reads. The backlight is currently controlled by a GPIO
level; it does not use PWM dimming. The motor component uses MCPWM.
This fixed board-level driver is a singleton for ESP32-S3; its example enables
PSRAM.

The display uses two RGB565 draw buffers, each holding one screen of pixels.
Double buffering lets drawing and SPI transfer alternate. The driver currently
has no display TE synchronization and does not guarantee tear-free scrolling or
large-area updates.

| Resource | Default GPIO | Ownership |
| --- | --- | --- |
| LCD SCLK / MOSI | 12 / 9 | Exclusive |
| LCD RST / DC / CS / BL | 10 / 3 / 5 / 7 | Exclusive |
| Touch RST / INT | 2 / 1 | Exclusive; INT is reserved for the wired controller, although reads currently use polling |
| Touch SDA / SCL | Supplied port; board defaults 41 / 40 | Shared I2C using identical ordered pins |
| Touch device address | `0x15` | Exclusive address reservation on its bus |

Initialization claims resources before configuring hardware. If any exclusive
GPIO is occupied, initialization fails without taking it from its owner. Other
RootMaker components cannot claim these GPIOs until successful cleanup.
SDA/SCL are outside the exclusive group, so the screen and ZXACC baseboard may
initialize in either order and share their bus. These rules cover resources
acquired through RootMaker APIs; direct ESP-IDF calls bypass the protection.

| Function | Semantics and parameters |
| --- | --- |
| `yoke_screen_with_lvgl_init(port)` | Initialize the LCD, touch and LVGL together; `port` selects only touch SDA/SCL, not LCD pins; return NULL on failure |
| `yoke_screen_deinit()` | Stop and release the screen; return `ESP_ERR_INVALID_STATE` if not initialized; success invalidates the display pointer |

Initialize once, then create the UI through LVGL. Include `esp_lvgl_port.h` and
use `lvgl_port_lock(0)` / `lvgl_port_unlock()` for UI access from other tasks.
Do not initialize or deinitialize while holding the LVGL lock, or deinitialize
from an LVGL callback. Stop all application UI access before cleanup. On failure
retry only `deinit()`; reinitialize only after successful cleanup.
Initialization failures roll back acquired resources. If the log reports
incomplete cleanup, retry `deinit()` before trying initialization again.

Use LVGL for all display content. The former `yoke_screen_draw_bitmap()` API has
been removed; migrate to `lv_image` or `lv_canvas`, as shown in the canvas example
above. Image/canvas buffers must remain valid while LVGL uses them; follow the
same locking rules when updating objects or their buffers.
See [`examples/screen_demo`](examples/screen_demo) for the touch example.

This component contains MIT and Apache-2.0 code. See `LICENSE` and
`THIRD_PARTY_NOTICES.md`.
