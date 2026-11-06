// ============================================================================
//  config.h  -  Bruce-D0WD pin map and build-time configuration
//
//  Board : ESP32-D0WD (classic ESP32, dual core)
//  Display: SSD1306 128x64 OLED over I2C (GPIO21 SDA / GPIO22 SCL)
//
//  All wireless front-ends share the VSPI bus (SCK/MISO/MOSI) and each has
//  its own chip-select line. Pins are chosen to avoid the ESP32 strapping
//  and flash pins where practical; adjust to match your own wiring.
// ============================================================================
#pragma once

// ---- Firmware identity ------------------------------------------------------
#define FW_NAME     "Bruce-D0WD"
#define FW_VERSION  "0.1.0"

// ---- OLED (I2C) -------------------------------------------------------------
#define OLED_SDA        21
#define OLED_SCL        22
#define OLED_WIDTH      128
#define OLED_HEIGHT     64
#define OLED_ADDR       0x3C    // common SSD1306 address (some are 0x3D)
#define OLED_RESET      -1      // shared / not used

// ---- Shared VSPI bus --------------------------------------------------------
#define SPI_SCK         18
#define SPI_MISO        19
#define SPI_MOSI        23

// ---- NRF24L01 (2.4 GHz transceiver) ----------------------------------------
#define NRF_CE          4
#define NRF_CSN         5

// ---- CC1101 (sub-GHz transceiver) ------------------------------------------
#define CC1101_CSN      27
#define CC1101_GDO0     2
#define CC1101_GDO2     25

// ---- RC522 RFID (13.56 MHz) -------------------------------------------------
#define RFID_CS         15
#define RFID_RST        0

// ---- Infrared ---------------------------------------------------------------
#define IR_TX_PIN       12
#define IR_RX_PIN       13

// ---- Navigation buttons (active-low, internal pull-ups) ---------------------
#define BTN_UP          32
#define BTN_DOWN        33
#define BTN_SELECT      14

// ---- Misc -------------------------------------------------------------------
#define STATUS_LED      -1      // set to a GPIO if your board has a user LED
