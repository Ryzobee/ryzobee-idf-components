# RGBW demo

Demonstrates the Yoke-RGBW driver: initializes a strip of 4 RGBW WS2812 LEDs
and cycles red, green, blue, white, and off.

## Hardware

| Item | Default |
| --- | --- |
| Expansion port | RS_PORT1 |
| LED strip data pin | PORT1_PIN1 (GPIO15) |
| LED count | 4 |

Change `rootmaker_rs_port1` or `RGBW_LED_NUM` in `main/rgbw_demo_main.c` to
match your connection and strip.

## Build and flash

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```
