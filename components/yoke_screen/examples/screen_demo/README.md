# Screen demo

Demonstrates the Yoke-Screen driver: initializes the ST7789 display, the
CST816T touch controller, and LVGL, then provides an interactive touch test.

## Hardware

Requires a RootMaker Yoke board (or one with the same pinout): the ST7789 is
on SPI3 and the CST816T touch controller uses the board system I2C pins. The
I2C peripheral is selected by `rootmaker_utils`; board pins are fixed by
`rootmaker_pin.h` in the component and cannot be changed at runtime.

## Build and flash

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

After boot, press or drag inside the blue test area. The red dot follows the
finger and the screen reports the pointer state, coordinates, and press count.
Press and release coordinates are also printed to the serial monitor. This
verifies the touch controller, LVGL input registration, coordinate direction,
and continuous drag events.
