# Yoke-KEYW

Designed for the **RootMaker development board (ESP32-S3)**.

ESP-IDF driver for the RootMaker Yoke-KEYW module: one button plus a GPIO
indicator LED. The button handling is built on the Espressif
[`espressif/button`](https://components.espressif.com/components/espressif/button)
component.

## Installation

```yaml
dependencies:
  ryzobeezero/yoke_keyw: "^1.0.0"
```

Run `idf.py reconfigure` (or `idf.py build`) to download it.

## Usage

Pass any two-pin RootMaker port to the driver. The first pin is the active-high
button and the second pin is the indicator LED.

```c
#include "esp_log.h"
#include "yoke_keyw.h"

static void on_event(button_event_t event, void *user_data)
{
    ESP_LOGI("demo", "button event: %d", event);
}

yoke_keyw_handle_t key = yoke_keyw_init(rootmaker_rs_port1);
ESP_ERROR_CHECK(key != NULL ? ESP_OK : ESP_FAIL);
ESP_ERROR_CHECK(yoke_keyw_register_event_callback(key, BUTTON_SINGLE_CLICK, on_event, NULL));
```

When only `BUTTON_SINGLE_CLICK` is registered, it runs immediately after every
short release, and a physical double-click invokes it twice. Registering
`BUTTON_DOUBLE_CLICK` enables the button component's click-classification
window: single-click handling is then delayed briefly so a double-click can be
reported as one distinct event.

The button device and LED GPIO are owned by the driver. Deinitialize each
instance with its matching `yoke_keyw_deinit()` call to release its resources.

## API

See [`include/yoke_keyw.h`](include/yoke_keyw.h).

## Requirements

* ESP-IDF 5.3 or later
* RootMaker development board (ESP32-S3)

## Example

See [`examples/keyw_demo`](examples/keyw_demo).

## Cleanup contract

`deinit(NULL)` returns `ESP_ERR_INVALID_ARG`. Successful deinitialization
invalidates the handle; set it to `NULL`. On error keep it for a cleanup retry
and stop all business operations. Stop application access before cleanup and
serialize lifecycle calls. Never reuse a successfully freed handle.
KEYW cleanup and long-press reconfiguration must not run in button or ESP timer callbacks. Button deletion is dispatched between scans on the ESP timer task.

If initialization fails and rollback also fails, the driver retains the
unfinished instance internally. The next initialization call retries cleanup
first and returns NULL while cleanup still fails. Serialize initialization
and deinitialization with all other RootMaker lifecycle calls.

## Usage and API semantics

`pins[0]` connects to the active-high button; `pins[1]` connects to the GPIO
indicator LED. Initialization exclusively claims both GPIOs, turns the indicator
off and sets the long-press threshold to 1500 ms. This is a separate driver from
the I2C button on the EC11 module.

| Function | Semantics and parameters |
| --- | --- |
| `yoke_keyw_init()` | Select the port and create the button instance; return NULL on failure |
| `yoke_keyw_set_led()` | `true` drives the LED high/on; `false` drives it low/off |
| `yoke_keyw_set_long_press_time()` | Set a nonzero threshold in milliseconds; recreate the button and reset gesture recognition |
| `yoke_keyw_register_event_callback()` | Subscribe one callback per event; duplicate subscriptions return `ESP_ERR_INVALID_STATE` |
| `yoke_keyw_unregister_event_callback()` | Remove a subscription; return `ESP_ERR_NOT_FOUND` if none exists |
| `yoke_keyw_deinit()` | Turn the indicator off, delete the button and release the GPIOs |

Initialize first, set the long-press threshold, register events, then run the
application. With only `BUTTON_SINGLE_CLICK` subscribed, each short release
immediately reports a single click, so two quick presses produce two single
clicks. Subscribing `BUTTON_DOUBLE_CLICK` enables the underlying multi-click
classification window: single clicks wait for classification and a double click
is reported as one event. `BUTTON_MULTIPLE_CLICK` requires a click-count parameter
and is unsupported by this simplified API.

Callbacks run on the ESP timer task and must remain short and non-blocking.
Do not create instances, modify subscriptions, deinitialize or change long-press
thresholds from any ESP timer callback. These configuration operations wait for
the timer task; calls from the instance's own button callback return an invalid
state error. Subscription updates are serialized with scanning. After successful
unsubscription, the removed callback is no longer running.

Send complex work to an application task through a queue. Keep `user_data` valid
while subscribed and while a callback is executing. Serialize configuration and
lifecycle operations on an instance, and stop application access before cleanup.
After success set the handle to NULL; after failure retain it only for cleanup
retry. See [`examples/keyw_demo`](examples/keyw_demo) for single-click LED toggling
and long-press handling.
