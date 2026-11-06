// ============================================================================
//  modules.h  -  wireless front-end bring-up and presence detection
//
//  Each function performs INITIALIZATION and a lightweight presence check so
//  the UI can report whether a given peripheral is wired up and responding.
//  No transmit/attack behaviour is implemented here - this is a scaffold onto
//  which authorized, lawful functionality can be added.
// ============================================================================
#pragma once
#include <Arduino.h>

struct ModuleStatus {
    bool wifi;      // on-chip Wi-Fi radio reachable
    bool bluetooth; // on-chip BLE controller reachable
    bool nrf24;     // NRF24L01 responded on SPI
    bool cc1101;    // CC1101 responded on SPI
    bool rfid;      // RC522 responded on SPI
    bool ir;        // IR TX/RX pins configured
    bool oled;      // SSD1306 panel acknowledged
};

namespace Modules {
    // Bring up the shared SPI bus once, before probing SPI peripherals.
    void beginBus();

    // Individual probes. Each returns true when the peripheral is present.
    bool probeWifi();
    bool probeBluetooth();
    bool probeNrf24();
    bool probeCC1101();
    bool probeRfid();
    bool probeIr();

    // Probe everything and fill a status struct.
    ModuleStatus scan();
}
