# RAD60 demo

Demonstrates the Yoke-RAD60 driver: initializes the radar on UART1 and prints
the cached multi-target detection data once per second.

## Hardware

| Item | Default |
| --- | --- |
| UART port | UART1 |
| Expansion port | RS_PORT1 |
| TX | PORT1_PIN1 (GPIO15) |
| RX | PORT1_PIN2 (GPIO16) |
| Baud rate | 921600 |

Change `rootmaker_rs_port1` in `main/rad60_demo_main.c` to select a different
expansion port.

## Build and flash

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Watch the `rad60_demo` log lines for communication status and object list.
