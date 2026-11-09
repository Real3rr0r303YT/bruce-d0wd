#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include <vector>

#define FW_VERSION "1.1.1"

// ---------- Pins ----------
#define PIN_SDA 21
#define PIN_SCL 22
#define OLED_ADDR 0x3C
#define PIN_BTN_SEL 14
#define PIN_BTN_UP 32
#define PIN_BTN_DOWN 33
#define PIN_NRF_CE 4
#define PIN_NRF_CSN 5
#define PIN_IR_TX 12
#define PIN_IR_RX 13

#define AP_SSID "ESP-MT"  // kurz halten: WIFI-QR muss in QR-Version 1 passen
#define HOSTNAME "esp-multitool"
#define IR_FILE "/ir_codes.txt"
#define NRF_CFG "/nrf.cfg"

enum Btn : uint8_t { BTN_NONE = 0, BTN_UP, BTN_DOWN, BTN_SEL, BTN_BACK };

// ---------- ui.cpp ----------
extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern uint8_t screenMirror[1024];
extern bool oledOk;
extern char uiTitleBuf[32];
void uiBegin();
Btn readButtons();
void uiFlush();
void drawHeader(const String &title);
// QR in normaler Polaritaet (dunkle Module auf hellem Grund) inkl. Quiet-Zone.
// Waehlt kleinste passende Version und groessten ganzzahligen Faktor fuer maxPx.
// Rueckgabe: gezeichnete Kantenlaenge in px.
int drawQR(const String &text, int x, int y, int maxPx, int quiet = 1);
int qrSizePx(const String &text, int maxPx, int quiet = 1);
void showMessage(const String &title, const String &l1, const String &l2 = "", const String &l3 = "");

struct ListView {
    String title;
    std::vector<String> items;
    int sel = 0;
    int top = 0;
    bool selectable = true;
    bool nav(Btn b);
    void draw();
};

// ---------- main.cpp (Fernsteuerung aus dem Webserver) ----------
void remotePushBtn(Btn b);
void remoteIrSend(const String &name);
void remoteNrfScan();
void remoteRun(const String &path);

// ---------- ir.cpp ----------
void irBegin();
void irRxStart();
void irRxStop();
bool irPoll(String &summary, String &line);
bool irSendLine(const String &line);
std::vector<String> irLoadNames();
String irGetLine(const String &name);
bool irSave(const String &name, const String &line);
bool irDelete(const String &name);
bool irRename(const String &from, const String &to);
String irNextName();

// ---------- radio.cpp ----------
extern bool nrfPresent;
extern uint8_t nrfLevel[126];
extern uint8_t nrfPeak[126];
extern volatile bool nrfScanning;
bool nrfDetect();
bool nrfScanStart();
void nrfScanSweep();
void nrfPeakReset();
bool nrfRxStart();
bool nrfRxPoll(String &hex);
void nrfRxSetChannel(int ch);
int nrfRxChannel();
String nrfRxInfo();
void nrfStop();

struct BleDev {
    String name;
    String mac;
    int rssi;
};
std::vector<BleDev> bleScan(uint32_t seconds);

// ---------- net.cpp ----------
extern bool apMode;
void netBegin();
void netLoop();
bool netHasSaved();
String netSavedSsid();
bool netConnectSaved(uint32_t timeoutMs);
void netStartAP();
void netResetWifi();
String netIP();
bool netConnected();
String fmtBytes(size_t b);
void fsListAll(std::vector<String> &names, std::vector<size_t> &sizes);

// ---------- script.cpp (.ino-Interpreter) ----------
bool scriptStart(const String &path);
void scriptStop();
bool scriptRunning();
String scriptFile();
String scriptLog();
String scriptError();
std::vector<String> scriptScreen();
void scriptBootCheck();
String scriptBootNote();
String resetReasonText();
