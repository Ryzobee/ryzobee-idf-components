# Motor demo

Demonstrates the Yoke-motor driver by running a brushed DC motor forward at a
fixed 60% speed.

## Hardware

| Item | Default |
| --- | --- |
| Expansion port | RS_PORT1 |
| H-bridge PWMA | PORT1_PIN1 (GPIO15) |
| H-bridge PWMB | PORT1_PIN2 (GPIO16) |

Change `rootmaker_rs_port1` in `main/motor_demo_main.c` to select a different
expansion port. Change `MOTOR_SPEED_PERCENT` to select a speed from 0 to 100.
The motor starts after initialization and keeps running forward.

## Build and flash

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```
