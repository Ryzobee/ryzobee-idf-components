# EC11 I2C Register Map

| Address | Function | Access | Description |
| --- | --- | --- | --- |
| `0x00` | ID high byte | R | Device ID, read-only. Writes are acknowledged but ignored. The fixed ID is `0x4543`. |
| `0x01` | ID low byte | R | |
| `0x02` | KEY | R | Button state and press count. |
| `0x03` | KEY press time | R | Duration of the most recent press, 0.1 s/LSB. A value of 10 represents 1 second. |
| `0x04` | CNT high byte | R | Signed 16-bit accumulated encoder count. Writes are acknowledged but ignored. |
| `0x05` | CNT low byte | R | |
| `0x06` | DIFF high byte | R | Signed 16-bit encoder delta. Writes are acknowledged but ignored. |
| `0x07` | DIFF low byte | R | **Reading this low byte clears the DIFF value.** |
| `0x08` | LED1 red brightness | R/W | |
| `0x09` | LED1 green brightness | R/W | LED1 is farther from the connector. |
| `0x0A` | LED1 blue brightness | R/W | |
| `0x0B` | LED2 red brightness | R/W | |
| `0x0C` | LED2 green brightness | R/W | LED2 is nearer to the connector. |
| `0x0D` | LED2 blue brightness | R/W | |
| `0x0E` | CTRL1 | R/W | Writing `0x5A` enters test mode; other values are stored without another defined action. |
| `0x0F` | CTRL2 | R/W | Configuration register 2; see its bit layout below. |
| Other | | | Writes are acknowledged but ignored; reads return `0xFF`. |

All defined registers support sequential reads and writes.

## KEY register (`0x02`)

| Bits | Name | Description |
| --- | --- | --- |
| bit7 | RAW | Debounced button state; 1 means pressed. |
| bit6 | Reserved | |
| bit5 | Reserved | |
| bit4 | Reserved | |
| bits3:0 | Press count | Latched completed-press count, up to 15; cleared after reading. A value of 3 means three press-and-release actions occurred since the previous read. |

The register has no fixed default value.

## CTRL2 register (`0x0F`)

| Bit | Name |
| --- | --- |
| bit7 | 1 |
| bit6 | !A2 |
| bit5 | !A1 |
| bit4 | !A0 |
| bit3 | 0 |
| bit2 | A2 |
| bit1 | A1 |
| bit0 | A0 |

The upper address bits are the inverse of the low three address bits. Invalid
complement values are acknowledged but ignored. A2:A0 select the low three
bits of the 7-bit device address. A valid write is saved to flash and takes
effect after the peripheral resets.
