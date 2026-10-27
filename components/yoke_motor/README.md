# Yoke-motor

Designed for the **RootMaker development board (ESP32-S3)**.

ESP-IDF driver for a brushed DC motor driven by an H-bridge on an MCPWM
peripheral. The low-level PWM handling uses the Espressif
[`espressif/bdc_motor`](https://components.espressif.com/components/espressif/bdc_motor)
component.

## Installation

```yaml
dependencies:
  ryzobeezero/yoke_motor: "^1.0.0"
```

Run `idf.py reconfigure` (or `idf.py build`) to download it.

## Usage

```c
#include "yoke_motor.h"

gpio_num_t motor_pins[] = { GPIO_NUM_19, GPIO_NUM_20 };
const rootmaker_port_t motor_port = {
    .type = ROOTMAKER_PORT_TYPE_RS_BUS,
    .pin_num = 2,
    .pins = motor_pins,
};
yoke_motor_handle_t motor = yoke_motor_init(motor_port);
ESP_ERROR_CHECK(motor != NULL ? ESP_OK : ESP_FAIL);

yoke_motor_set_speed_percent(motor, 60);
yoke_motor_forward(motor);       // spin forward at 60%
yoke_motor_reverse(motor);       // spin in reverse
yoke_motor_brake(motor);         // slow-decay stop
yoke_motor_coast(motor);         // fast-decay stop
```

## API

See [`include/yoke_motor.h`](include/yoke_motor.h). Speeds are set as a percent
(0-100) of the configured PWM period.

## Requirements

* ESP-IDF 5.3 or later
* RootMaker development board (ESP32-S3) and its motor module

## Example

See [`examples/motor_demo`](examples/motor_demo).

Multiple motors can use different ports. Initialization tries MCPWM groups in
order, moving to the next group only when the current group has no available
resource. Other errors are returned through initialization failure.

## Cleanup contract

`deinit(NULL)` returns `ESP_ERR_INVALID_ARG`. Successful deinitialization
invalidates the handle; set it to `NULL`. On error keep it for a cleanup retry
and stop all business operations. Stop application access before cleanup and
serialize lifecycle calls. Never reuse a successfully freed handle.

If initialization fails and hardware cleanup also fails, the driver retains the
unfinished instance internally. The next `yoke_motor_init()` retries that cleanup
before creating another instance. While the error persists it returns `NULL`
and retains the original GPIO claims; it does not allocate additional instances.

## Usage and API semantics

The motor drives an H-bridge using **MCPWM PWM outputs**. `pins[0]` is PWMA and
`pins[1]` is PWMB; both GPIOs are claimed exclusively. Initialization leaves the
motor coasting with a preset duty of 50%; it does not start forward motion.
The PWM frequency is 20 kHz with a timer resolution of 10 MHz.

| Function | Semantics and parameters |
| --- | --- |
| `yoke_motor_init()` | Create an instance and find an available MCPWM group automatically; return NULL on failure |
| `yoke_motor_set_speed_percent()` | Set PWM duty from 0 to 100 percent, not closed-loop speed; leave direction and stop mode unchanged |
| `yoke_motor_forward()` | Drive forward at the configured duty |
| `yoke_motor_reverse()` | Drive in reverse at the configured duty |
| `yoke_motor_coast()` | Stop by coasting and retain the configured duty |
| `yoke_motor_brake()` | Apply electrical braking and retain the configured duty |
| `yoke_motor_deinit()` | Coast, disable and delete MCPWM resources, then release the GPIOs |

The usual sequence is: initialize, set duty, select direction, stop as required,
then release. After `coast()` or `brake()`, call a direction function to resume;
`set_speed_percent()` alone does not resume motion. The driver does not implement
acceleration ramps or reversal delays. The application controls reversal timing
and duty.

Instances must use non-overlapping GPIOs; their number is limited by MCPWM
resources. Serialize operations on each instance and stop application access
before cleanup. Set the handle to NULL after successful cleanup; retain it only
for cleanup retry after failure. See [`examples/motor_demo`](examples/motor_demo).

This component contains MIT and Apache-2.0 code. See `LICENSE` and
`THIRD_PARTY_NOTICES.md`.
