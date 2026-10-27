# ZXACC-maker I2C Interface

ZXACC-maker is a power-management baseboard with a PY32F002B slave controller.
The ESP32-S3 reads battery status and configures button-controlled power
behavior over I2C.

## Default connection

| Item | Value |
| --- | --- |
| 7-bit I2C address | `0x73` |
| I2C clock | 100 kHz |
| ESP32-S3 I2C controller | Selected automatically by `rootmaker_utils` |
| SCL | GPIO40 |
| SDA | GPIO41 |

> `0x73` is a **7-bit address**. Pass it directly to the ESP-IDF
> `i2c_master_*` APIs; do not shift it into the `0xE6/0xE7` wire-format
> read/write addresses.

`yoke_zxacc_maker_init(rootmaker_sys_i2c_port)` uses the supplied system-I2C port and device
address. It acquires or reuses the shared bus through `rootmaker_utils` and
holds one bus reference after successful initialization.
`yoke_zxacc_maker_deinit()` removes only its device and releases one reference.
The underlying bus is deleted after its final user releases it.

## Register protocol

All multibyte values use little-endian byte order (least-significant byte
first), matching the `yoke_zxacc_maker` driver and the `py32_test` example.

| Address | Access | Data | Description | API |
| --- | --- | --- | --- | --- |
| `0x00` | Read | `uint16_t` | PY32 firmware version | `get_firmware_version()` |
| `0x02` | Read | `uint8_t` | Charge state: 0 charging, 1 not charging, 2 fully charged | `get_charge_state()` |
| `0x03` | Read | `uint32_t` | Battery voltage in mV | `get_battery_voltage()` |
| `0x07` | Write | `0x01` | Shut down immediately | `shutdown()` |
| `0x08` | Read/Write | `uint16_t` | Power-on button hold time in ms | `get/set_wakeup_time()` |
| `0x0A` | Read/Write | `uint16_t` | Shutdown button hold time in ms | `get/set_shutdown_time()` |
| `0x0C` | Write | `uint32_t` | Timed wake-up delay in seconds; writing it enters low-power mode until the timer wakes the board | `set_timer_wakeup()` |

Registers `0x07` and `0x0C` change the board's power state. During debugging,
make sure an expected power transition will not unexpectedly disconnect the
serial console or programming tool.
