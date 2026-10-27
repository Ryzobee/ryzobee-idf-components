# Yoke-RAD60

Designed for the **RootMaker development board (ESP32-S3)**.

ESP-IDF driver for the RootMaker RAD60 UART radar module. The driver runs its
own UART receive/processing tasks and keeps the latest detection data in a
cache that the application reads without waiting for a new UART frame.
The snapshot mutex can wait up to 100 ms.

## Installation

```yaml
dependencies:
  ryzobeezero/yoke_rad60: "^1.0.0"
```

Run `idf.py reconfigure` (or `idf.py build`) to download it.

## Usage

```c
#include "yoke_rad60.h"
#include "rootmaker_utils.h"

static gpio_num_t radar_pins[] = { GPIO_NUM_15, GPIO_NUM_16 };
static rootmaker_port_t radar_port = {
    .type = ROOTMAKER_PORT_TYPE_RS_BUS,
    .pin_num = 2,
    .pins = radar_pins,
};

yoke_rad60_radar_data_t data;
yoke_rad60_handle_t radar = yoke_rad60_init(radar_port, YOKE_RAD60_DEFAULT_UART_NUM);
ESP_ERROR_CHECK(radar != NULL ? ESP_OK : ESP_FAIL);
esp_err_t ret = yoke_rad60_get_radar_data(radar, &data);
// ESP_ERR_NOT_SUPPORTED means communication is not healthy yet.
if (ret == ESP_OK && data.is_detected) {
    printf("objects: %u\n", data.radar_info.obj_num);
}
```

To select a different UART controller while keeping the RAD60 fixed baud rate,
pass the controller as the second argument:

```c
yoke_rad60_handle_t radar2 = yoke_rad60_init(rootmaker_rs_port2, UART_NUM_2);
ESP_ERROR_CHECK(radar2 != NULL ? ESP_OK : ESP_FAIL);
```

The component intentionally keeps a narrow surface area now: init, deinit, and
snapshot reads from the latest cached radar data.

## UART pin conflict note

The RAD60 default pins (GPIO15/16) may conflict with other board peripherals;
pass the actual TX/RX GPIOs through the `rootmaker_port_t` you give to
`yoke_rad60_init()`.

## Requirements

* ESP-IDF 5.3 or later
* RootMaker development board (ESP32-S3)

## Example

See [`examples/rad60_demo`](examples/rad60_demo).

## Multiple instances and lifecycle

Each handle owns its UART, receive buffer, queues, tasks, and detection cache.
Use different UART controllers and non-overlapping signal GPIOs for each radar.
An already installed UART is rejected without reconfiguring it.
Call `yoke_rad60_deinit(radar)` to stop and release only that instance.
On cleanup failure the handle remains available for a cleanup retry; reads are
rejected once cleanup has started. Stop application reads before deinitializing.
Initialization and deinitialization must be serialized with other RootMaker
component lifecycle calls. Instance count is limited by available UARTs and memory.

If initialization fails and rollback also fails, the driver retains the
unfinished instance internally. The next initialization call retries cleanup
first and returns NULL while cleanup still fails. Serialize initialization
and deinitialization with all other RootMaker lifecycle calls.

## Usage and API semantics

`pins[0]` is the ESP32 UART TX signal (connect to module RX); `pins[1]` is UART RX
(connect to module TX). Both signals are claimed exclusively. The UART uses
921600 bit/s, 8 data bits, no parity and 1 stop bit. `YOKE_RAD60_DEFAULT_UART_NUM`
is UART1; the port descriptor selects the signal pins.

Sharing RS_PORT1 with EC11 causes a GPIO conflict. The integration demo defaults
to P1 and checks active port claims; select separate ports for simultaneous
peripherals.

| Function | Semantics and parameters |
| --- | --- |
| `yoke_rad60_init(port, uart_num)` | Acquire an unused UART and start receive/processing tasks; return NULL on failure; success does not confirm radar communication |
| `yoke_rad60_get_radar_data()` | Read the latest cached snapshot without waiting for a new UART frame; the mutex may wait up to 100 ms |
| `yoke_rad60_deinit()` | Request task shutdown, wait for exit, then release queues, UART and GPIOs; may block, so do not call from callbacks or ISRs |

Handle snapshot return values separately:

- `ESP_OK`: the snapshot is valid and `is_comm_ok=true`; read targets only when
  `is_detected` is true.
- `ESP_ERR_NOT_SUPPORTED`: communication is unhealthy or no valid frame has
  arrived yet. The output snapshot is still written, but it is not a valid
  measurement of target absence.
- `ESP_ERR_TIMEOUT`: the snapshot lock was not acquired and output was not
  updated. Do not use the current output on other errors either.

`radar_info.obj_num` is the active target count, up to 2. Access only
`obj[0..obj_num-1]`. Communication health is cached by the background task;
reading a snapshot does not perform an immediate link probe.

Multiple readers may access snapshots, but stop all readers before cleanup.
Each instance needs a different UART and non-overlapping GPIOs. Set the handle
to NULL after successful cleanup; on failure retain it only for cleanup retry.
See [`examples/rad60_demo`](examples/rad60_demo).
