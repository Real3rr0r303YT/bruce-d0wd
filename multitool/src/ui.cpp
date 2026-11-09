#include "shared.h"
#include <WiFi.h>
#include <Wire.h>
#include <qrcode.h>

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);
uint8_t screenMirror[1024];
bool oledOk = false;
char uiTitleBuf[32] = "Start";

static const int ROWS = 4;  // sichtbare Listenzeilen
static const int RH = 12;   // Zeilenhoehe
static const int Y0 = 16;   // Start unterhalb der gelben Zone

void uiBegin() {
    pinMode(PIN_BTN_SEL, INPUT_PULLUP);
    pinMode(PIN_BTN_UP, INPUT_PULLUP);
    pinMode(PIN_BTN_DOWN, INPUT_PULLUP);

    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.beginTransmission(OLED_ADDR);
    oledOk = (Wire.endTransmission() == 0);

    u8g2.setI2CAddress(OLED_ADDR * 2);
    u8g2.setBusClock(400000);
    u8g2.begin();
    u8g2.enableUTF8Print();
    u8g2.setFontMode(1);
}

void uiFlush() {
    u8g2.sendBuffer();
    memcpy(screenMirror, u8g2.getBufferPtr(), sizeof(screenMirror));
}

// ---------- Tasten ----------
// Hoch/Runter feuern beim Loslassen (damit Hoch+Runter = Zurueck erkannt wird),
// halten = Auto-Repeat. Select feuert beim Druecken.
Btn readButtons() {
    static uint32_t lastSample = 0;
    static bool pUp = false, pDn = false, pSel = false;
    static bool combo = false, repeated = false;
    static uint32_t holdStart = 0, lastRep = 0;

    uint32_t now = millis();
    if (now - lastSample < 20) return BTN_NONE;
    lastSample = now;

    bool up = digitalRead(PIN_BTN_UP) == LOW;
    bool dn = digitalRead(PIN_BTN_DOWN) == LOW;
    bool sel = digitalRead(PIN_BTN_SEL) == LOW;
    Btn out = BTN_NONE;

    if (up && dn && !combo) {
        combo = true;
        out = BTN_BACK;
    }

    if ((up != dn) && !combo) {
        if (!pUp && !pDn) {
            holdStart = now;
            repeated = false;
        } else if (now - holdStart > 450 && now - lastRep > 110) {
            lastRep = now;
            repeated = true;
            out = up ? BTN_UP : BTN_DOWN;
        }
    }

    if (!up && pUp && !combo && !repeated && !dn) out = BTN_UP;
    if (!dn && pDn && !combo && !repeated && !up) out = BTN_DOWN;
    if (!up && !dn) {
        combo = false;
        if (!pUp && !pDn) repeated = false;
    }

    if (sel && !pSel) out = BTN_SEL;

    pUp = up;
    pDn = dn;
    pSel = sel;
    return out;
}

// ---------- Zeichnen ----------
void drawHeader(const String &title) {
    String t = title;
    u8g2.setFont(u8g2_font_7x13B_tf);
    while (t.length() > 1 && u8g2.getUTF8Width(t.c_str()) > 100) t.remove(t.length() - 1);
    u8g2.drawUTF8(0, 12, t.c_str());

    u8g2.setFont(u8g2_font_5x7_tf);
    const char *st = apMode ? "AP" : (WiFi.status() == WL_CONNECTED ? "WiFi" : "--");
    u8g2.drawStr(128 - u8g2.getStrWidth(st), 7, st);
    strlcpy(uiTitleBuf, title.c_str(), sizeof(uiTitleBuf));
}

bool ListView::nav(Btn b) {
    int n = items.size();
    if (n == 0) return false;
    if (!selectable) {
        int maxTop = n > ROWS ? n - ROWS : 0;
        if (b == BTN_UP) top = top > 0 ? top - 1 : 0;
        else if (b == BTN_DOWN) top = top < maxTop ? top + 1 : maxTop;
        else return false;
        return true;
    }
    if (b == BTN_UP) sel = (sel - 1 + n) % n;
    else if (b == BTN_DOWN) sel = (sel + 1) % n;
    else return false;
    if (sel < top) top = sel;
    if (sel >= top + ROWS) top = sel - ROWS + 1;
    return true;
}

void ListView::draw() {
    drawHeader(title);
    u8g2.setFont(u8g2_font_6x10_tf);
    int n = items.size();
    u8g2.setClipWindow(0, Y0, 123, 64);
    for (int i = 0; i < ROWS && top + i < n; i++) {
        int idx = top + i;
        int y = Y0 + i * RH;
        if (selectable && idx == sel) {
            u8g2.setDrawColor(1);
            u8g2.drawBox(0, y, 123, RH);
            u8g2.setDrawColor(0);
        }
        u8g2.drawUTF8(2, y + 10, items[idx].c_str());
        u8g2.setDrawColor(1);
    }
    u8g2.setMaxClipWindow();
    if (n > ROWS) {
        int h = max(4, 48 * ROWS / n);
        int y = Y0 + (48 - h) * top / (n - ROWS);
        u8g2.drawBox(125, y, 3, h);
    }
}

// ---------- QR ----------
// Kapazitaet ECC_LOW (Version 1..4): Byte- und Alphanumerik-Modus
static const uint8_t CAP_BYTE[] = {17, 32, 53, 78};
static const uint8_t CAP_ALNUM[] = {25, 47, 77, 114};

static bool isAlnumQR(const String &t) {
    const char *ok = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
    for (unsigned i = 0; i < t.length(); i++)
        if (!strchr(ok, t[i])) return false;
    return true;
}

static uint8_t qrVersionFor(const String &t) {
    const uint8_t *cap = isAlnumQR(t) ? CAP_ALNUM : CAP_BYTE;
    for (uint8_t v = 1; v <= 4; v++)
        if (t.length() <= cap[v - 1]) return v;
    return 5;
}

int qrSizePx(const String &text, int maxPx, int quiet) {
    int modules = 17 + 4 * qrVersionFor(text) + 2 * quiet;
    int s = maxPx / modules;
    if (s < 1) s = 1;
    return modules * s;
}

int drawQR(const String &text, int x, int y, int maxPx, int quiet) {
    QRCode qr;
    uint8_t buf[260];
    uint8_t ver = qrVersionFor(text);
    qrcode_initText(&qr, buf, ver, ECC_LOW, text.c_str());
    int modules = qr.size + 2 * quiet;
    int s = maxPx / modules;
    if (s < 1) s = 1;
    int box = modules * s;
    int off = quiet * s;
    u8g2.setDrawColor(1);
    u8g2.drawBox(x, y, box, box);  // heller Hintergrund + Quiet-Zone
    u8g2.setDrawColor(0);
    for (uint8_t my = 0; my < qr.size; my++)
        for (uint8_t mx = 0; mx < qr.size; mx++)
            if (qrcode_getModule(&qr, mx, my)) u8g2.drawBox(x + off + mx * s, y + off + my * s, s, s);
    u8g2.setDrawColor(1);
    return box;
}

void showMessage(const String &title, const String &l1, const String &l2, const String &l3) {
    u8g2.clearBuffer();
    drawHeader(title);
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawUTF8(0, 30, l1.c_str());
    u8g2.drawUTF8(0, 44, l2.c_str());
    u8g2.drawUTF8(0, 58, l3.c_str());
    uiFlush();
}
