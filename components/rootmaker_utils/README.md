# RootMaker Utils

Designed for the **RootMaker development board (ESP32-S3)**.

ESP-IDF helper component for RootMaker boards. It provides the interface port
model and an I2C master bus pool shared by the peripheral drivers
(`yoke_ec11`, `yoke_motor`, `yoke_rad60`, `yoke_rgbw`, ...).

## Installation

```yaml
dependencies:
  ryzobeezero/rootmaker_utils: "^1.0.0"
```

Run `idf.py reconfigure` (or `idf.py build`) to download it.

## Port model

A `rootmaker_port_t` describes pins; it does not allocate hardware. Predefined
board descriptors are exported as `rootmaker_rs_port1` through `rootmaker_rs_port4`,
`rootmaker_s_bus`, `rootmaker_f_bus`, and `rootmaker_sys_i2c_port`.
Use them by value with peripheral initializers. `S_BUS` and `F_BUS` describe entire
connector groups; a two-pin driver only reads their first two entries. Define a
custom descriptor to select specific connector pins:

```c
#include "rootmaker_utils.h"

static const gpio_num_t custom_pins[] = { GPIO_NUM_15, GPIO_NUM_16 };
static const rootmaker_port_t custom_port = {
    .type = ROOTMAKER_PORT_TYPE_RS_BUS,
    .pin_num = 2,
    .pins = custom_pins,
};
```

Peripheral drivers copy the pins they use. Direct users of `rootmaker_use_port()`
must keep their pin array alive and unchanged until release. Do not redefine
predefined descriptor names in application code.

Drivers that are port-aware take a `rootmaker_port_t` instead of raw pins, e.g.:

```c
gpio_num_t ec11_pins[] = { GPIO_NUM_15, GPIO_NUM_16 };
const rootmaker_port_t ec11_port = {
    .type = ROOTMAKER_PORT_TYPE_RS_BUS,
    .pin_num = 2,
    .pins = ec11_pins,
};
yoke_ec11_handle_t ec11 = yoke_ec11_init(ec11_port);
ESP_ERROR_CHECK(ec11 != NULL ? ESP_OK : ESP_FAIL);
```

## I2C bus pool

`rootmaker_new_i2c_bus()` creates a bus on the first available controller or
reuses the bus for an identical ordered SDA/SCL pair. Disjoint pairs use separate
controllers. Partial overlap or swapped SDA/SCL is rejected, as are exclusive
GPIO claims. SDA and SCL must be different valid GPIOs. Externally created buses
are not adopted; their occupied controllers are skipped.

Each successful acquisition owns one reference. Remove your I2C devices and
address reservations before releasing it with `rootmaker_del_i2c_bus()`.
The bus is deleted only after its final reference is released. Serialize all
RootMaker resource claims/releases across tasks. The registry tracks only
resources acquired through these APIs; direct ESP-IDF calls bypass it.

## API

See [`include/rootmaker_utils.h`](include/rootmaker_utils.h).

## Requirements

* ESP-IDF 5.3 or later
* RootMaker development board (ESP32-S3)

## Usage and resource rules

Peripheral drivers acquire and release their own resources. Applications normally
call the peripheral's `init()` directly; pre-claiming its port with
`rootmaker_use_port()` or `rootmaker_new_i2c_bus()` can cause duplicate claims.

| Board descriptor | Pin order | Purpose |
| --- | --- | --- |
| `rootmaker_rs_port1` | GPIO15, GPIO16 | Two-signal expansion port |
| `rootmaker_rs_port2` | GPIO19, GPIO20 | Two-signal expansion port |
| `rootmaker_rs_port3` | GPIO13, GPIO14 | Two-signal expansion port |
| `rootmaker_rs_port4` | GPIO17, GPIO18 | Two-signal expansion port |
| `rootmaker_sys_i2c_port` | SDA=GPIO41, SCL=GPIO40 | Shared touch/power baseboard bus |
| `rootmaker_s_bus` / `rootmaker_f_bus` | See `include/rootmaker_pin.h` | Whole connector groups; define a custom descriptor for a subset of signals |

These predefined pins belong to the RootMaker board. They are not a wiring guide
for arbitrary ESP32 boards. `type` identifies the connector; it does not select a
driver. GPIO numbers determine conflicts. A descriptor does not claim hardware.
Two-signal drivers use the first two entries; RGBW uses only the first entry.

| Function | Usage rules |
| --- | --- |
| `rootmaker_get_port_pins()` | Read the first two pins without modifying the descriptor; missing or invalid pins return `ESP_ERR_INVALID_ARG` |
| `rootmaker_get_port_pin()` | Read one valid GPIO by its zero-based index |
| `rootmaker_use_port()` | Exclusively claim every GPIO in the descriptor; keep the pin array alive and unchanged until release |
| `rootmaker_release_port()` | Release a claim using the same type, pin count and pin order |
| `rootmaker_port_is_used()` | Return true if any pin is claimed exclusively or belongs to a shared I2C bus; this alone does not determine whether I2C sharing is possible |
| `rootmaker_new_i2c_bus()` | Reuse an identical ordered SDA/SCL pair and acquire one reference; select the controller automatically |
| `rootmaker_del_i2c_bus()` | Release one reference per successful acquisition; remove the caller's devices first |
| `rootmaker_claim_i2c_address()` | Reserve a 7-bit device address on one bus; duplicate reservations fail |
| `rootmaker_release_i2c_address()` | Release the address after removing the device, then release the bus reference |

I2C sharing requires identical ordered SDA/SCL pins. Partial overlap or swapped
SDA/SCL returns `ESP_ERR_INVALID_STATE`. SDA=SCL returns `ESP_ERR_INVALID_ARG`.
Devices on a shared bus may use different addresses; each address has one owner.
Serialize all resource acquisitions, releases and address changes in the
application. Direct ESP-IDF GPIO/I2C calls bypass this resource manager and its
conflict protection.
