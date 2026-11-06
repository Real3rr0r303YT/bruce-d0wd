// ============================================================================
//  modules.cpp  -  wireless front-end bring-up and presence detection
// ============================================================================
#include "modules.h"
#include "config.h"

#include <SPI.h>
#include <WiFi.h>
#include <esp_bt.h>

#include <RF24.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <MFRC522.h>
#include <IRsend.h>
#include <IRrecv.h>

// SPI peripheral driver instances (shared VSPI bus)
static RF24     nrf(NRF_CE, NRF_CSN);
static MFRC522  rc522(RFID_CS, RFID_RST);

namespace Modules {

void beginBus() {
    SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
}

// --- Wi-Fi: bring the radio up in station mode and read back the MAC --------
bool probeWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, false);
    delay(50);
    String mac = WiFi.macAddress();
    // A valid, non-zero MAC means the radio initialized.
    return mac.length() == 17 && mac != "00:00:00:00:00:00";
}

// --- Bluetooth: check the BT controller is idle/available -------------------
bool probeBluetooth() {
    esp_bt_controller_status_t st = esp_bt_controller_get_status();
    // Any status other than "not initialized by a crash" indicates the
    // controller hardware is present on this ESP32 variant.
    return st == ESP_BT_CONTROLLER_STATUS_IDLE ||
           st == ESP_BT_CONTROLLER_STATUS_INITED ||
           st == ESP_BT_CONTROLLER_STATUS_ENABLED;
}

// --- NRF24L01: isChipConnected() reads back a known register ----------------
bool probeNrf24() {
    if (!nrf.begin()) return false;
    bool ok = nrf.isChipConnected();
    nrf.powerDown();
    return ok;
}

// --- CC1101: the driver reads the part/version register over SPI ------------
bool probeCC1101() {
    ELECHOUSE_cc1101.setSpiPin(SPI_SCK, SPI_MISO, SPI_MOSI, CC1101_CSN);
    ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);
    ELECHOUSE_cc1101.Init();
    return ELECHOUSE_cc1101.getCC1101();   // true when version register sane
}

// --- RC522 RFID: read the firmware/version register -------------------------
bool probeRfid() {
    rc522.PCD_Init();
    byte v = rc522.PCD_ReadRegister(MFRC522::VersionReg);
    // 0x00 / 0xFF are the classic "nothing there" responses.
    return v != 0x00 && v != 0xFF;
}

// --- IR: no bus to probe; just configure the pins ---------------------------
bool probeIr() {
    pinMode(IR_TX_PIN, OUTPUT);
    digitalWrite(IR_TX_PIN, LOW);
    pinMode(IR_RX_PIN, INPUT);
    return true;
}

ModuleStatus scan() {
    ModuleStatus s{};
    s.wifi      = probeWifi();
    s.bluetooth = probeBluetooth();
    s.nrf24     = probeNrf24();
    s.cc1101    = probeCC1101();
    s.rfid      = probeRfid();
    s.ir        = probeIr();
    s.oled      = true;  // set by the caller after Display::begin()
    return s;
}

} // namespace Modules

// Small helper used by the UI layer (declared in main.cpp) so that WiFi.h
// stays out of the display/header files.
String WiFi_macToString() {
    return WiFi.macAddress();
}
