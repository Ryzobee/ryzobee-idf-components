# Ec11 demo

Demonstrates the Yoke-EC11 I2C driver by printing encoder and button state
changes and setting both onboard LEDs to one fixed color. Input is polled every
20 ms, while unchanged state is not logged.

## Hardware

| Item | Default |
| --- | --- |
| I2C port | Automatically selected by `rootmaker_utils` |
| Expansion port | RS_PORT1 |
| SDA | PORT1_PIN1 (GPIO15) |
| SCL | PORT1_PIN2 (GPIO16) |

Change `rootmaker_rs_port1` in `main/ec11_demo_main.c` to select a different
expansion port. The driver acquires or reuses the matching bus through
`rootmaker_utils`.

Requires ESP-IDF 5.5.3 or later.

## Build and flash

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

The console prints the device ID plus encoder and button changes. Both LEDs
remain at a fixed color so their write interface is easy to verify.
