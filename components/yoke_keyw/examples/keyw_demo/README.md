# KEYW demo

Demonstrates the Yoke-KEYW driver: initializes a button + indicator LED module,
then uses single click to toggle the LED and long press to log.

## Hardware

| Item | Default |
| --- | --- |
| Expansion port | RS_PORT1 |
| Button | PORT1_PIN1 (GPIO15, active high) |
| LED | PORT1_PIN2 (GPIO16) |

Change `rootmaker_rs_port1` in `main/keyw_demo_main.c` to select a different
expansion port.

## Build and flash

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```
