# Yoke-EC11

Designed for the **RootMaker development board (ESP32-S3)**.

ESP-IDF driver for the RootMaker Yoke-EC11-V10 I2C peripheral: a rotary encoder,
a button, and two onboard WS2812 LEDs controlled through one I2C slave.

## Installation

Add the component to your project's `idf_component.yml`:

```yaml
dependencies:
  ryzobeezero/yoke_ec11: "^1.0.0"
```

Then run `idf.py reconfigure` (or `idf.py build`) to download it from the ESP
Component Registry.

## Usage

The driver acquires or reuses an I2C master bus through `rootmaker_utils` using
the first two pins of the supplied port. Other devices using the same pins can
share that bus.

```c
#include "yoke_ec11.h"

gpio_num_t ec11_pins[] = { GPIO_NUM_15, GPIO_NUM_16 };
rootmaker_port_t ec11_port = {
    .type = ROOTMAKER_PORT_TYPE_RS_BUS,
    .pin_num = 2,
    .pins = ec11_pins,
};

yoke_ec11_handle_t ec11 = yoke_ec11_init(ec11_port);
ESP_ERROR_CHECK(ec11 != NULL ? ESP_OK : ESP_FAIL);

int16_t count;
yoke_ec11_read_encoder_count(ec11, &count);

yoke_ec11_rgb_t color = { .red = 0, .green = 255, .blue = 0 };
yoke_ec11_set_ws2812(ec11, 0, color);
```

For a peripheral that uses a non-default address, or for custom bus timing,
use the complete configuration interface:

```c
yoke_ec11_config_t config = YOKE_EC11_CONFIG_DEFAULT();
config.device_address = 0x73;
config.scl_speed_hz = 400000;
config.timeout_ms = 500;

yoke_ec11_handle_t ec11 = yoke_ec11_init_with_config(rootmaker_rs_port1, &config);
ESP_ERROR_CHECK(ec11 != NULL ? ESP_OK : ESP_FAIL);
```

## API

See [`include/yoke_ec11.h`](include/yoke_ec11.h) for the full API. Key
operations:

* `yoke_ec11_init()` — initialize with the default address, speed, and timeout.
* `yoke_ec11_init_with_config()` — initialize with complete configuration.
* `yoke_ec11_deinit()` — remove the I2C device.
* `yoke_ec11_read_encoder_count()` / `yoke_ec11_read_encoder_diff()` — rotary
  encoder position and per-read delta.
* `yoke_ec11_read_key()` — debounced button state and press counter.
* `yoke_ec11_set_ws2812()` / `yoke_ec11_set_ws2812_all()` — drive the two
  onboard LEDs.
* `yoke_ec11_configure_slave_address()` — persist a new low-three-bit slave
  address (takes effect after peripheral reset).

The I2C register map is documented in [`docs/iic.md`](docs/iic.md).

## Requirements

* ESP-IDF 5.5.3 or later (uses `i2c_master_device_change_address`)
* RootMaker development board (ESP32-S3)

## Example

See [`examples/ec11_demo`](examples/ec11_demo).

## Device binding

Initialization rejects an address already reserved by EC11 or ZXACC on the same
I2C bus. Different buses can use the same address. Successful device removal
releases the reservation. Serialize initialization, deinitialization, and address
changes; this registry does not track devices added directly through ESP-IDF.
Active address changes reject reserved target addresses and keep the old binding on failure.

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

The encoder, its button and its two RGB LEDs are accessed through I2C.
`port.pins[0]` is SDA and `pins[1]` is SCL. Defaults are address `0x70`, a 100 kHz
clock and a 1000 ms transaction timeout. Initialization reads and checks the
device ID; a missing module, wrong address or unexpected ID returns NULL.
The minimum supported ESP-IDF version is 5.5.3, matching the validated environment.

```c
#include "esp_log.h"
#include "yoke_ec11.h"

void app_main(void) {
    yoke_ec11_handle_t encoder = yoke_ec11_init(rootmaker_rs_port1);
    if (encoder == NULL) {
        ESP_LOGE("demo", "EC11 initialization failed; check wiring and address");
        return;
    }
    int16_t count;
    esp_err_t ret = yoke_ec11_read_encoder_count(encoder, &count);
    if (ret == ESP_OK) {
        ESP_LOGI("demo", "count=%d", count);
    }
    ESP_ERROR_CHECK(yoke_ec11_deinit(encoder));
    encoder = NULL;
}
```

| Function | Semantics and parameters |
| --- | --- |
| `yoke_ec11_init()` | Use defaults; return an independent instance on success or NULL on failure |
| `yoke_ec11_init_with_config()` | Set address, speed and timeout; the read-only configuration is copied and need not outlive initialization |
| `yoke_ec11_probe()` | Check whether the current target address responds without changing peripheral configuration |
| `yoke_ec11_read_device_id()` | Return the device ID through the output pointer |
| `yoke_ec11_read_key()` | Return the current button state and press count; reading clears the peripheral's accumulated press count |
| `yoke_ec11_read_key_press_time_ms()` | Return milliseconds with 100 ms resolution |
| `yoke_ec11_read_encoder_count()` | Return the signed accumulated encoder count |
| `yoke_ec11_read_encoder_diff()` | Return the signed delta; reading clears the peripheral's delta counter |
| `yoke_ec11_set_ws2812()` / `yoke_ec11_get_ws2812()` | `index` is 0 or 1; each RGB channel ranges from 0 to 255 |
| `yoke_ec11_set_ws2812_all()` | Set both RGB LEDs in one write |
| `yoke_ec11_read_ctrl1()` / `yoke_ec11_write_ctrl1()` | Access the raw control register |
| `yoke_ec11_enter_test_mode()` | Write the test-mode command; ordinary applications do not need this operation |
| `yoke_ec11_configure_slave_address()` | Write address bits 0-2 (value 0-7); the change takes effect after peripheral reset and does not update the host handle |
| `yoke_ec11_set_active_address()` | Change only the host's 7-bit target address for subsequent transactions; do not send a peripheral address-change or reset command |
| `yoke_ec11_deinit()` | Remove the device, release the address reservation, then release one shared bus reference |

To change the peripheral address, write its configuration at the old address,
reset the module externally, then update the host target address. Alternatively,
release the old instance and initialize at the new address. Host address changes
reject addresses already reserved on the same bus.

Hardware validation confirmed `0x70 -> 0x71 -> 0x70` using power cycles between
writes. The module continued responding at the previous address immediately
after each write and used the saved address after its power was cycled. Resetting
only the ESP32-S3 does not establish that the EC11 peripheral has restarted.

Use read outputs only after `ESP_OK`. Poll read-to-clear values from one
application task so multiple readers do not consume each other's data.
Serialize lifecycle and address-change operations; stop application access before
cleanup. Successful cleanup invalidates the handle. On failure, retain it only
for cleanup retry. See [`examples/ec11_demo`](examples/ec11_demo) for polling.
