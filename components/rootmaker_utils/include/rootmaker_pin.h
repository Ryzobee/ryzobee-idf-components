#pragma once

/** @file rootmaker_pin.h
 *  @brief GPIO assignments for the RootMaker board.
 *
 *  Connector macro suffixes identify connector pin positions; the values are
 *  ESP32-S3 GPIO numbers. Pins shared by multiple connectors keep the same GPIO.
 */

// Expansion port identifiers shared by KEYW, MOTO, RGBW, and RAD60.
typedef enum {
    RS_PORT1 = 0,
    RS_PORT2,
    RS_PORT3,
    RS_PORT4,
} rootmaker_rs_port_t;

// Board peripherals

// UART0
#define UART0_TX 43
#define UART0_RX 44

// On-board LED and button
#define PIN_RGB_LED 45
#define BTN_PIN 0

// System I2C, shared with the touch panel and power baseboard
#define BOARD_SDA 41
#define BOARD_SCL 40

// Motion sensor interrupt
#define SEN_INT 42

// LCD (SDA is SPI MOSI; RS is data/command)
#define LCD_SDA 9
#define LCD_SCK 12
#define LCD_CS 5
#define LCD_RS 3
#define LCD_RST 10
#define LCD_BL 7

// Touch panel (shares the system I2C bus)
#define TP_SDA 41
#define TP_SCL 40
#define TP_INT 1
#define TP_RST 2

// SPI bus
#define SPI_MOSI 31
#define SPI_MISO 32
#define SPI_SCK 30
#define SPI_SS 29
#define SPI_WP 28
#define SPI_HD 27

// Thermal module UART
#define THERMAL_UART_TX 4
#define THERMAL_UART_RX 6

// Expansion connectors

// Two-pin RS ports
#define PORT1_PIN1 15
#define PORT1_PIN2 16

#define PORT2_PIN1 19
#define PORT2_PIN2 20

#define PORT3_PIN1 13
#define PORT3_PIN2 14

#define PORT4_PIN1 17
#define PORT4_PIN2 18

// F_BUS connector
#define F_BUS9 20
#define F_BUS10 19
#define F_BUS11 12
#define F_BUS12 11
#define F_BUS13 10
#define F_BUS14 9
#define F_BUS15 8
#define F_BUS16 7
#define F_BUS17 6
#define F_BUS18 5
#define F_BUS19 4
#define F_BUS20 3
#define F_BUS21 2
#define F_BUS22 1
#define F_BUS23 BOARD_SDA
#define F_BUS24 BOARD_SCL

// S_BUS connector
#define S_BUS1 48
#define S_BUS2 47
#define S_BUS3 46
#define S_BUS4 38
#define S_BUS5 39
#define S_BUS6 33
#define S_BUS7 35
#define S_BUS8 BOARD_SCL
#define S_BUS9 BOARD_SDA
#define S_BUS10 45
#define S_BUS11 37
#define S_BUS12 36
#define S_BUS13 34
#define S_BUS14 21
