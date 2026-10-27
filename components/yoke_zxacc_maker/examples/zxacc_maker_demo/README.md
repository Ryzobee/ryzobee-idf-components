# ZXACC-maker demo

Demonstrates the Yoke-ZXACC-maker driver: initializes the power-management
baseboard and prints firmware version, battery voltage, charge state, and the
button power timing once per second.

## Hardware

| Item | Default |
| --- | --- |
| I2C port | Automatically selected by `rootmaker_utils` |
| SDA | GPIO41 |
| SCL | GPIO40 |
| 7-bit I2C address | `0x73` |

The example uses the fixed RootMaker board pinout and the driver's default
address. The bus is acquired through `rootmaker_utils` and released by
`yoke_zxacc_maker_deinit()` when the device is no longer needed.

## Build and flash

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Watch the `zxacc_maker_demo` log lines for the board status.
