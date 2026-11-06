// ============================================================================
//  Bruce-D0WD  -  main.cpp
//
//  A hardware bring-up scaffold for an ESP32-D0WD multi-tool dev board with an
//  SSD1306 OLED. On boot it initializes the display and probes each wireless
//  front-end (Wi-Fi, Bluetooth, NRF24, CC1101, RFID, IR), then presents a
//  simple button-driven menu.
//
//  SCOPE: this firmware only initializes hardware and reports module presence.
//  It intentionally contains no transmit / attack functionality. Any further
//  capability you add is your responsibility and must comply with the laws and
//  regulations of your jurisdiction and be used only on systems you own or are
//  explicitly authorized to test.
//
//  License: GNU GPL v3.0 (see LICENSE)
// ============================================================================
#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "modules.h"

static ModuleStatus g_status;

// WiFi.macAddress() wrapper (defined in modules.cpp) kept out of the headers so
// WiFi.h stays isolated to one translation unit.
String WiFi_macToString();

// ----- Top-level menu --------------------------------------------------------
static const char* kMainMenu[] = {
    "Module Status",
    "Wi-Fi Info",
    "Bluetooth Info",
    "NRF24",
    "CC1101 (Sub-GHz)",
    "IR",
    "RFID (RC522)",
    "About",
};
static const int kMainMenuCount = sizeof(kMainMenu) / sizeof(kMainMenu[0]);

// ----- Simple debounced button helper ---------------------------------------
struct Button {
    uint8_t pin;
    bool    last;
    uint32_t t;
    bool pressed() {
        bool now = (digitalRead(pin) == LOW);       // active-low
        bool edge = now && !last && (millis() - t > 150);
        if (now != last) { last = now; t = millis(); }
        return edge;
    }
};

static Button btnUp{BTN_UP, false, 0};
static Button btnDown{BTN_DOWN, false, 0};
static Button btnSelect{BTN_SELECT, false, 0};

static int g_selected = 0;

// ----- Page renderers --------------------------------------------------------
static void showModuleStatus() {
    char w[20], b[20], n[20], c[20], r[20], i[20];
    snprintf(w, sizeof(w), "Wi-Fi     : %s", g_status.wifi ? "OK" : "--");
    snprintf(b, sizeof(b), "Bluetooth : %s", g_status.bluetooth ? "OK" : "--");
    snprintf(n, sizeof(n), "NRF24     : %s", g_status.nrf24 ? "OK" : "--");
    snprintf(c, sizeof(c), "CC1101    : %s", g_status.cc1101 ? "OK" : "--");
    snprintf(r, sizeof(r), "RFID      : %s", g_status.rfid ? "OK" : "--");
    snprintf(i, sizeof(i), "IR        : %s", g_status.ir ? "OK" : "--");
    const char* lines[] = {w, b, n, c, r, i};
    Display::drawStatus("Module Status", lines, 6);
}

static void showWifiInfo() {
    String mac = WiFi_macToString();
    char l1[24];
    snprintf(l1, sizeof(l1), "MAC %s", mac.c_str());
    const char* lines[] = {
        g_status.wifi ? "Radio: ready (STA)" : "Radio: not ready",
        l1,
        "Scan/connect: add in",
        "your own build.",
    };
    Display::drawStatus("Wi-Fi", lines, 4);
}

static void showSimple(const char* title, bool present, const char* bus) {
    char l1[24];
    snprintf(l1, sizeof(l1), "Detected : %s", present ? "yes" : "no");
    const char* lines[] = { l1, bus };
    Display::drawStatus(title, lines, 2);
}

static void showAbout() {
    const char* lines[] = {
        FW_NAME " v" FW_VERSION,
        "ESP32-D0WD + SSD1306",
        "Educational scaffold.",
        "GPLv3 - authorized",
        "use only.",
    };
    Display::drawStatus("About", lines, 5);
}

// ----- Setup -----------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println();
    Serial.println(FW_NAME " v" FW_VERSION " booting...");

    pinMode(BTN_UP, INPUT_PULLUP);
    pinMode(BTN_DOWN, INPUT_PULLUP);
    pinMode(BTN_SELECT, INPUT_PULLUP);

    bool oledOk = Display::begin();
    if (!oledOk) {
        Serial.println("[WARN] SSD1306 not found on I2C - check wiring/address");
    } else {
        Display::splash();
    }

    Modules::beginBus();
    delay(600);                 // let the splash show briefly

    g_status = Modules::scan();
    g_status.oled = oledOk;

    Serial.printf("WiFi:%d BT:%d NRF24:%d CC1101:%d RFID:%d IR:%d OLED:%d\n",
                  g_status.wifi, g_status.bluetooth, g_status.nrf24,
                  g_status.cc1101, g_status.rfid, g_status.ir, g_status.oled);

    Display::drawMenu("Bruce-D0WD", kMainMenu, kMainMenuCount, g_selected);
}

// ----- Main loop -------------------------------------------------------------
void loop() {
    bool dirty = false;

    if (btnUp.pressed()) {
        g_selected = (g_selected - 1 + kMainMenuCount) % kMainMenuCount;
        dirty = true;
    }
    if (btnDown.pressed()) {
        g_selected = (g_selected + 1) % kMainMenuCount;
        dirty = true;
    }

    if (btnSelect.pressed()) {
        switch (g_selected) {
            case 0: showModuleStatus(); break;
            case 1: showWifiInfo(); break;
            case 2: showSimple("Bluetooth", g_status.bluetooth, "On-chip BLE ctrl"); break;
            case 3: showSimple("NRF24", g_status.nrf24, "SPI CE4 CSN5"); break;
            case 4: showSimple("CC1101", g_status.cc1101, "SPI CSN27"); break;
            case 5: showSimple("IR", g_status.ir, "TX12 RX13"); break;
            case 6: showSimple("RFID", g_status.rfid, "RC522 SPI CS15"); break;
            case 7: showAbout(); break;
        }
        delay(1500);                // hold the page, then return to the menu
        dirty = true;
    }

    if (dirty) {
        Display::drawMenu("Bruce-D0WD", kMainMenu, kMainMenuCount, g_selected);
    }
    delay(10);
}
