# Yoke-RGBW

Designed for the **RootMaker development board (ESP32-S3)**.

ESP-IDF driver for RGBW WS2812 LED strips driven through RMT.

## Installation

```yaml
dependencies:
  ryzobeezero/yoke_rgbw: "^1.0.0"
```

Run `idf.py reconfigure` (or `idf.py build`) to download it.

## Usage

```c
#include <assert.h>
#include "yoke_rgbw.h"

gpio_num_t rgbw_pins[] = { GPIO_NUM_13 };
rootmaker_port_t rgbw_port = {
    .type = ROOTMAKER_PORT_TYPE_RS_BUS,
    .pin_num = 1,
    .pins = rgbw_pins,
};
yoke_rgbw_handle_t rgbw = yoke_rgbw_init(rgbw_port, 4);
assert(rgbw != NULL);

ESP_ERROR_CHECK(yoke_rgbw_set_all(rgbw, 255, 0, 0, 0));  // red
ESP_ERROR_CHECK(yoke_rgbw_set_all(rgbw, 0, 255, 0, 0));  // green
ESP_ERROR_CHECK(yoke_rgbw_set_all(rgbw, 0, 0, 0, 0));    // off

// Set LED 2 to blue (index is zero-based).
ESP_ERROR_CHECK(yoke_rgbw_set_pixel(rgbw, 2, 0, 0, 255, 0));

ESP_ERROR_CHECK(yoke_rgbw_deinit(rgbw));
```

Colors use GRBW byte order, as expected by most RGBW WS2812 strips.

## API

See [`include/yoke_rgbw.h`](include/yoke_rgbw.h).

## Requirements

* ESP-IDF 5.3 or later
* RootMaker development board (ESP32-S3)

## Example

See [`examples/rgbw_demo`](examples/rgbw_demo).

## Cleanup contract

`deinit(NULL)` returns `ESP_ERR_INVALID_ARG`. Successful deinitialization
invalidates the handle; set it to `NULL`. On error keep it for a cleanup retry
and stop all business operations. Stop application access before cleanup and
serialize lifecycle calls. Never reuse a successfully freed handle.

If initialization fails and rollback also fails, the driver retains the
unfinished instance internally. The next initialization call retries cleanup
first and returns NULL while cleanup still fails. Serialize initialization
and deinitialization with all other RootMaker lifecycle calls.

## Usage and API semantics

The driver uses RMT to transmit RGBW strip data in four-byte GRBW order. It uses
and exclusively claims only `pins[0]`; the second GPIO of a two-signal port
remains available. `led_num` must be greater than zero. The EC11 module's two
three-channel RGB LEDs use the EC11 I2C API instead of this strip driver.

| Function | Semantics and parameters |
| --- | --- |
| `yoke_rgbw_init(port, led_num)` | Create the pixel buffer and RMT channel; return NULL on failure; do not transmit an off frame |
| `yoke_rgbw_set_all()` | Accept 0-255 per RGBW channel; update the entire pixel buffer and immediately transmit the strip |
| `yoke_rgbw_set_pixel()` | Accept indices 0 through `led_num-1`; preserve other cached pixels and immediately transmit the entire strip |
| `yoke_rgbw_deinit()` | Release RMT resources, the pixel buffer and GPIO claim; do not transmit an off frame |

Each color-setting call waits for RMT transmission to finish, so do not call it
from an ISR or ESP timer callback. No separate refresh call is needed. To turn
the strip off during startup or shutdown, explicitly call
`yoke_rgbw_set_all(handle, 0, 0, 0, 0)`.

If multiple tasks use one strip, serialize access in the application to protect
the transmit buffer. Successful cleanup invalidates the handle; set it to NULL.
On failure, stop setting colors and retain the handle only for cleanup retry.
See [`examples/rgbw_demo`](examples/rgbw_demo).

Each transmission ends with a continuous 300 us low reset interval, including
back-to-back calls. This is a conservative engineering choice. Data-bit timing
and GRBW order remain as implemented; confirm the display behavior on the board's
actual strip.
