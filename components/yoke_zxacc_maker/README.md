# Yoke-ZXACC-maker

Designed for the **RootMaker development board (ESP32-S3)**.

ESP-IDF driver for the RootMaker ZXACC-maker power-management baseboard: a
PY32F002B I2C slave that reports battery voltage and charge state and controls
button power timing, timed wake-up, and shutdown.

## Installation

```yaml
dependencies:
  ryzobeezero/yoke_zxacc_maker: "^1.0.0"
```

Run `idf.py reconfigure` (or `idf.py build`) to download it.

## Usage

Pass `rootmaker_sys_i2c_port` to use the on-board system I2C (SCL=GPIO40, SDA=GPIO41). The driver
automatically acquires or reuses the matching bus through `rootmaker_utils`.
The default device address is `0x73`.

```c
#include "esp_log.h"
#include "yoke_zxacc_maker.h"

yoke_zxacc_maker_handle_t board = yoke_zxacc_maker_init(rootmaker_sys_i2c_port);
if (board == NULL) {
    ESP_LOGE("app", "ZXACC-maker initialization failed");
    return;
}

uint32_t voltage_mv;
yoke_zxacc_maker_get_battery_voltage(board, &voltage_mv);
yoke_zxacc_maker_charge_state_t charge;
yoke_zxacc_maker_get_charge_state(board, &charge);

uint16_t wake_ms;
yoke_zxacc_maker_get_wakeup_time(board, &wake_ms);
yoke_zxacc_maker_set_wakeup_time(board, 2000);

/* Call after all board API users have stopped. */
yoke_zxacc_maker_deinit(board);
```

`yoke_zxacc_maker_set_timer_wakeup()` and `yoke_zxacc_maker_shutdown()` change
the board power state, so confirm your serial console and flashing tool will
survive the power cut.

On the tested baseboard firmware `0x0101`,
`yoke_zxacc_maker_set_timer_wakeup(board, 8)` also cycles the main-controller
power with USB connected and starts it again after approximately eight seconds. Application execution starts from boot; initialize
peripherals again after waking. This I2C command behavior differs from the
USB-powered button behavior described in the schematic notes.

The I2C address is fixed at `0x73` by the current board API. It is a 7-bit
address, so pass it directly to ESP-IDF APIs and do not shift it into the
read/write form.

## API

See [`include/yoke_zxacc_maker.h`](include/yoke_zxacc_maker.h). The register
map and byte order are documented in
[`docs/iic.md`](docs/iic.md).

## Requirements

* ESP-IDF 5.3 or later
* RootMaker development board (ESP32-S3)

## Example

See [`examples/zxacc_maker_demo`](examples/zxacc_maker_demo).

## Device binding

Initialization rejects an address already reserved by EC11 or ZXACC on the same
I2C bus. Different buses can use the same address. Successful device removal
releases the reservation. Serialize initialization, deinitialization, and address
changes; this registry does not track devices added directly through ESP-IDF.

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

This I2C power-management baseboard uses `pins[0]` as SDA and `pins[1]` as SCL.
The board default is `rootmaker_sys_i2c_port` (GPIO41 / GPIO40), shared with the
touch controller. The fixed device address is `0x73`, the clock is 100 kHz and
each transaction has a 1000 ms timeout. Initialization creates a device handle
without probing hardware; the first successful read confirms a response.

| Function | Semantics and parameters |
| --- | --- |
| `yoke_zxacc_maker_init()` / `yoke_zxacc_maker_deinit()` | Create/release an independent instance; duplicate instances at the same bus/address are rejected |
| `yoke_zxacc_maker_get_firmware_version()` | Return the raw 16-bit version field |
| `yoke_zxacc_maker_get_battery_voltage()` | Return voltage in mV through the output pointer |
| `yoke_zxacc_maker_get_charge_state()` | Return the charge-state enum; unrecognized raw values map to `CHARGE_UNKNOWN` |
| `yoke_zxacc_maker_get_wakeup_time()` / `set_wakeup_time()` | Read/set the power-on button long-press threshold in ms; this is not a timed wake-up delay |
| `yoke_zxacc_maker_get_shutdown_time()` / `set_shutdown_time()` | Read/set the power-off button long-press threshold in ms |
| `yoke_zxacc_maker_set_timer_wakeup()` | Set a timed wake-up delay in seconds; the baseboard enters low power after the write |
| `yoke_zxacc_maker_shutdown()` | Request immediate power-off, which may remove power from the main controller |

Use read outputs only after `ESP_OK`. Reads and writes are synchronous I2C
transactions; do not wait for them in ISRs or timer callbacks. The existing
power-key timing function names are retained for compatibility. Distinguish them
by units: long-press thresholds use milliseconds, while `set_timer_wakeup()`
uses seconds. See [`docs/iic.md`](docs/iic.md) for the underlying protocol.

A successful operation confirms the I2C transaction; the baseboard performs the
power transition. Stop application access before cleanup. Set the handle to NULL
after success; after failure retain it only for cleanup retry.
See [`examples/zxacc_maker_demo`](examples/zxacc_maker_demo).
