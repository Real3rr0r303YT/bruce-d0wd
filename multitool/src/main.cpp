// ESP Multitool – ESP32-D0WD + SSD1306 128x64 + 3 Tasten
#include "shared.h"
#include <LittleFS.h>
#include <WiFi.h>

enum Scr {
    S_HOME,
    S_MAIN,
    S_IR,
    S_IR_REC,
    S_IR_DONE,
    S_IR_LIST,
    S_WIFI,
    S_WIFI_SCAN,
    S_WIFI_NET,
    S_WIFI_INFO,
    S_BLE,
    S_BLE_DEV,
    S_NRF,
    S_NRF_SCAN,
    S_NRF_RX,
    S_FILES,
    S_SCRIPTS,
    S_SCRIPT_RUN,
    S_STATUS
};

static Scr scr = S_HOME;
static ListView lv;
static int mainSel = 0, irSel = 0, wifiSel = 0, nrfSel = 0, netSel = 0, bleSel = 0;
static String capSummary, capLine;
static std::vector<String> irNames;
struct Net {
    String ssid, bssid;
    int rssi, ch;
    bool open;
};
static std::vector<Net> nets;
static std::vector<BleDev> bles;
static std::vector<String> nrfLog;
static int nrfCursor = 40;
static bool qrFull = false;
static std::vector<String> scriptFiles;
static int scriptSel = 0;
static char pendRun[64];
static volatile bool pendRunFlag = false;  // Vollbild-QR (Hoch/Runter auf dem Startscreen)
static String toast;
static uint32_t toastUntil = 0, lastInfo = 0;

static void enter(Scr s);

// ---------- Fernsteuerung (Webserver-Task -> Hauptschleife) ----------
static portMUX_TYPE rmux = portMUX_INITIALIZER_UNLOCKED;
static Btn rq[8];
static uint8_t rqH = 0, rqT = 0;
static char pendIr[48];
static volatile bool pendIrFlag = false, pendNrf = false;

void remotePushBtn(Btn b) {
    portENTER_CRITICAL(&rmux);
    uint8_t n = (rqH + 1) % 8;
    if (n != rqT) {
        rq[rqH] = b;
        rqH = n;
    }
    portEXIT_CRITICAL(&rmux);
}

static Btn remotePop() {
    Btn b = BTN_NONE;
    portENTER_CRITICAL(&rmux);
    if (rqT != rqH) {
        b = rq[rqT];
        rqT = (rqT + 1) % 8;
    }
    portEXIT_CRITICAL(&rmux);
    return b;
}

void remoteIrSend(const String &name) {
    portENTER_CRITICAL(&rmux);
    strlcpy(pendIr, name.c_str(), sizeof(pendIr));
    pendIrFlag = true;
    portEXIT_CRITICAL(&rmux);
}

void remoteNrfScan() { pendNrf = true; }

void remoteRun(const String &path) {
    portENTER_CRITICAL(&rmux);
    strlcpy(pendRun, path.c_str(), sizeof(pendRun));
    pendRunFlag = true;
    portEXIT_CRITICAL(&rmux);
}

static bool isScriptFile(const String &n) {
    String l = n;
    l.toLowerCase();
    return l.endsWith(".ino") || l.endsWith(".txt");
}

// Text auf Zeilen zu max. w Zeichen umbrechen
static void wrapInto(std::vector<String> &out, const String &t, int w) {
    int s = 0, L = t.length();
    while (s < L) {
        int nl = t.indexOf('\n', s);
        int e = nl < 0 ? L : nl;
        if (e - s > w) e = s + w;
        out.push_back(t.substring(s, e));
        s = (nl >= 0 && e == nl) ? e + 1 : e;
    }
}

// ---------- Hilfen ----------
static void showToast(const String &t) {
    toast = t;
    toastUntil = millis() + 1500;
}

static void setList(const String &t, const std::vector<String> &items, int sel = 0, bool selectable = true) {
    lv.title = t;
    lv.items = items;
    lv.selectable = selectable;
    int n = items.size();
    lv.sel = n ? constrain(sel, 0, n - 1) : 0;
    lv.top = lv.sel > 3 ? lv.sel - 3 : 0;
}

static void setInfo(const String &t, const std::vector<String> &lines) {
    int keepTop = (lv.title == t && !lv.selectable) ? lv.top : 0;
    setList(t, lines, 0, false);
    int maxTop = (int)lines.size() > 4 ? lines.size() - 4 : 0;
    lv.top = min(keepTop, maxTop);
}

static String uptimeStr() {
    uint32_t s = millis() / 1000;
    char b[20];
    snprintf(b, sizeof(b), "%luh %02lum %02lus", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
             (unsigned long)(s % 60));
    return b;
}

static std::vector<String> wifiLines() {
    std::vector<String> v;
    if (apMode) {
        v.push_back("Modus: Access Point");
        v.push_back("SSID: " AP_SSID);
        v.push_back("IP: " + WiFi.softAPIP().toString());
        v.push_back("Clients: " + String(WiFi.softAPgetStationNum()));
    } else if (WiFi.status() == WL_CONNECTED) {
        v.push_back("SSID: " + WiFi.SSID());
        v.push_back("IP: " + WiFi.localIP().toString());
        v.push_back("RSSI: " + String(WiFi.RSSI()) + " dBm");
        v.push_back("Kanal: " + String(WiFi.channel()));
        v.push_back("GW: " + WiFi.gatewayIP().toString());
        v.push_back("MAC: " + WiFi.macAddress());
        v.push_back("mDNS: " HOSTNAME ".local");
    } else {
        v.push_back("Nicht verbunden");
        v.push_back(netHasSaved() ? String("Gesp.: " + netSavedSsid()) : String("Kein WLAN gespeichert"));
    }
    return v;
}

static std::vector<String> statusLines() {
    std::vector<String> v = wifiLines();
    v.push_back("RAM frei: " + fmtBytes(ESP.getFreeHeap()));
    v.push_back("FS: " + fmtBytes(LittleFS.usedBytes()) + "/" + fmtBytes(LittleFS.totalBytes()));
    v.push_back(String("OLED: ") + (oledOk ? "ok" : "?"));
    v.push_back(String("NRF24: ") + (nrfPresent ? "erkannt" : "fehlt"));
    v.push_back("IR: TX 12 / RX 13");
    v.push_back("Uptime: " + uptimeStr());
    v.push_back("Firmware: v" FW_VERSION);
    v.push_back("Neustart: " + resetReasonText());
    return v;
}

static void buildNetList(int sel) {
    std::vector<String> it;
    for (auto &n : nets) {
        String s = n.ssid.length() ? n.ssid : String("(versteckt)");
        if (s.length() > 13) s = s.substring(0, 13);
        it.push_back(s + " " + String(n.rssi) + (n.open ? "" : "*"));
    }
    if (it.empty()) it.push_back("(keine Netze)");
    setList("Netze (" + String(nets.size()) + ")", it, sel, !nets.empty());
}

static void buildBleList(int sel) {
    std::vector<String> it;
    it.push_back("> Neu scannen");
    for (auto &b : bles) {
        String s = b.name.length() ? b.name : b.mac.substring(9);
        if (s.length() > 14) s = s.substring(0, 14);
        it.push_back(s + " " + String(b.rssi));
    }
    setList("BLE (" + String(bles.size()) + ")", it, sel);
}

static void leave(Scr from, Scr to) {
    bool toIr = (to == S_IR_REC || to == S_IR_DONE);
    if ((from == S_IR_REC || from == S_IR_DONE) && !toIr) irRxStop();
    if ((from == S_NRF_SCAN || from == S_NRF_RX) && to != from) nrfStop();
}

static void go(Scr s) {
    leave(scr, s);
    scr = s;
    enter(s);
}

static void enter(Scr s) {
    switch (s) {
    case S_HOME:
        break;
    case S_MAIN:
        setList("Hauptmenü",
                {"IR Fernbedienung", "WLAN", "Bluetooth (Scan)", "NRF24 2,4 GHz", "Skripte (.ino)", "Dateien", "Status / Info",
                 "QR / Web-Zugang"},
                mainSel);
        break;
    case S_IR:
        setList("IR", {"Aufnehmen", "Gespeicherte Codes"}, irSel);
        break;
    case S_IR_REC:
        irRxStart();
        break;
    case S_IR_DONE:
        setList(capSummary, {"Speichern", "Abspielen (Test)", "Neu aufnehmen"});
        break;
    case S_IR_LIST:
        irNames = irLoadNames();
        if (irNames.empty()) setList("IR Codes", {"(keine Codes)", "Erst aufnehmen oder", "per Web hochladen"}, 0, false);
        else setList("IR Codes", irNames);
        break;
    case S_WIFI:
        setList("WLAN",
                {"Info", "Netze scannen", "Verbinden (gesp.)", "QR anzeigen", "Onboarding (AP)", "WLAN zurücksetzen"},
                wifiSel);
        break;
    case S_WIFI_SCAN: {
        showMessage("WLAN", "Scanne Netze ...");
        int n = WiFi.scanNetworks(false, true);
        nets.clear();
        for (int i = 0; i < n; i++)
            nets.push_back({WiFi.SSID(i), WiFi.BSSIDstr(i), (int)WiFi.RSSI(i), (int)WiFi.channel(i),
                            WiFi.encryptionType(i) == WIFI_AUTH_OPEN});
        WiFi.scanDelete();
        buildNetList(0);
        break;
    }
    case S_WIFI_NET: {
        Net &n = nets[netSel];
        setList(n.ssid.length() ? n.ssid : String("(versteckt)"),
                {"RSSI: " + String(n.rssi) + " dBm", "Kanal: " + String(n.ch),
                 String("Sicherung: ") + (n.open ? "offen" : "WPA/WPA2"), n.bssid,
                 "Verbinden: Web-UI", "oder Onboarding (AP)"},
                0, false);
        break;
    }
    case S_WIFI_INFO:
        setInfo("WLAN Info", wifiLines());
        break;
    case S_BLE:
        showMessage("Bluetooth", "Scanne BLE (5 s) ...", "nur Empfang");
        bles = bleScan(5);
        buildBleList(1);
        break;
    case S_BLE_DEV: {
        BleDev &b = bles[bleSel];
        setList(b.name.length() ? b.name : String("(ohne Namen)"),
                {"MAC:", b.mac, "RSSI: " + String(b.rssi) + " dBm", b.name.length() ? String("Name: " + b.name) : String("Name: -")},
                0, false);
        break;
    }
    case S_NRF:
        setList("NRF24", {"Spektrum-Scanner", "Empfänger", "Modul prüfen"}, nrfSel);
        break;
    case S_NRF_SCAN:
        if (!nrfScanStart()) {
            scr = S_NRF;
            enter(S_NRF);
            showToast("NRF24 fehlt!");
        }
        break;
    case S_NRF_RX:
        nrfLog.clear();
        if (!nrfRxStart()) {
            scr = S_NRF;
            enter(S_NRF);
            showToast("NRF24 fehlt!");
        }
        break;
    case S_FILES: {
        std::vector<String> n;
        std::vector<size_t> sz;
        fsListAll(n, sz);
        std::vector<String> it;
        for (size_t i = 0; i < n.size(); i++) it.push_back(n[i] + " " + fmtBytes(sz[i]));
        if (it.empty()) it.push_back("(keine Dateien)");
        setList("Dateien (" + String(n.size()) + ")", it, 0, false);
        break;
    }
    case S_STATUS:
        setInfo("Status", statusLines());
        break;
    case S_SCRIPTS: {
        std::vector<String> n;
        std::vector<size_t> sz;
        fsListAll(n, sz);
        scriptFiles.clear();
        for (auto &f : n)
            if (isScriptFile(f)) scriptFiles.push_back(f);
        std::vector<String> it;
        for (auto &f : scriptFiles) it.push_back(f.substring(1));
        if (it.empty()) setList("Skripte", {"(keine .ino Datei)", "Im Web-UI unter", "Skript schreiben", "oder hochladen"}, 0, false);
        else setList("Skripte (" + String(it.size()) + ")", it, scriptSel);
        break;
    }
    case S_SCRIPT_RUN:
        break;
    }
}

// ---------- Eingaben ----------
static void handle(Btn b) {
    if (b == BTN_NONE) return;
    switch (scr) {
    case S_HOME:
        if (b == BTN_UP || b == BTN_DOWN) qrFull = !qrFull;
        else if (b == BTN_BACK && qrFull) qrFull = false;
        else if (b == BTN_SEL || b == BTN_BACK) {
            qrFull = false;
            go(S_MAIN);
        }
        break;

    case S_MAIN:
        if (lv.nav(b)) mainSel = lv.sel;
        else if (b == BTN_BACK) go(S_HOME);
        else if (b == BTN_SEL) {
            mainSel = lv.sel;
            const Scr t[] = {S_IR, S_WIFI, S_BLE, S_NRF, S_SCRIPTS, S_FILES, S_STATUS, S_HOME};
            go(t[lv.sel]);
        }
        break;

    case S_IR:
        if (lv.nav(b)) irSel = lv.sel;
        else if (b == BTN_BACK) go(S_MAIN);
        else if (b == BTN_SEL) go(lv.sel == 0 ? S_IR_REC : S_IR_LIST);
        break;

    case S_IR_REC:
        if (b == BTN_BACK) go(S_IR);
        break;

    case S_IR_DONE:
        if (lv.nav(b)) break;
        if (b == BTN_BACK) go(S_IR);
        else if (b == BTN_SEL) {
            if (lv.sel == 0) {
                String n = irNextName();
                showToast(irSave(n, capLine) ? String("Gespeichert: " + n) : String("Fehler beim Speichern"));
                go(S_IR);
            } else if (lv.sel == 1) {
                showToast(irSendLine(capLine) ? "Gesendet" : "Protokoll n. unterst.");
            } else go(S_IR_REC);
        }
        break;

    case S_IR_LIST:
        if (lv.nav(b)) break;
        if (b == BTN_BACK) go(S_IR);
        else if (b == BTN_SEL && !irNames.empty()) {
            String n = irNames[lv.sel];
            showToast(irSendLine(irGetLine(n)) ? String("Gesendet: " + n) : String("Senden fehlgeschl."));
        }
        break;

    case S_WIFI:
        if (lv.nav(b)) wifiSel = lv.sel;
        else if (b == BTN_BACK) go(S_MAIN);
        else if (b == BTN_SEL) {
            wifiSel = lv.sel;
            switch (lv.sel) {
            case 0: go(S_WIFI_INFO); break;
            case 1: go(S_WIFI_SCAN); break;
            case 2:
                if (!netHasSaved()) showToast("Kein WLAN gespeichert");
                else {
                    showMessage("WLAN", "Verbinde mit", netSavedSsid());
                    bool ok = netConnectSaved(15000);
                    if (!ok) netStartAP();
                    showToast(ok ? "Verbunden" : "Fehlgeschlagen -> AP");
                }
                break;
            case 3: go(S_HOME); break;
            case 4:
                netStartAP();
                go(S_HOME);
                break;
            case 5:
                netResetWifi();
                showToast("WLAN gelöscht");
                go(S_HOME);
                break;
            }
        }
        break;

    case S_WIFI_SCAN:
        if (lv.nav(b)) break;
        if (b == BTN_BACK) go(S_WIFI);
        else if (b == BTN_SEL && !nets.empty()) {
            netSel = lv.sel;
            go(S_WIFI_NET);
        }
        break;

    case S_WIFI_NET:
        if (lv.nav(b)) break;
        if (b == BTN_BACK) {
            scr = S_WIFI_SCAN;
            buildNetList(netSel);
        }
        break;

    case S_WIFI_INFO:
        if (lv.nav(b)) break;
        if (b == BTN_BACK) go(S_WIFI);
        break;

    case S_STATUS:
        if (lv.nav(b)) break;
        if (b == BTN_BACK) go(S_MAIN);
        break;

    case S_FILES:
        if (lv.nav(b)) break;
        if (b == BTN_BACK) go(S_MAIN);
        break;

    case S_SCRIPTS:
        if (lv.nav(b)) {
            scriptSel = lv.sel;
            break;
        }
        if (b == BTN_BACK) go(S_MAIN);
        else if (b == BTN_SEL && !scriptFiles.empty()) {
            scriptSel = lv.sel;
            if (scriptStart(scriptFiles[lv.sel])) go(S_SCRIPT_RUN);
            else showToast("Start fehlgeschlagen");
        }
        break;

    case S_SCRIPT_RUN:
        if (b == BTN_BACK) {
            if (scriptRunning()) {
                scriptStop();
                showToast("Stoppe ...");
            } else go(S_SCRIPTS);
        } else if (b == BTN_SEL && !scriptRunning()) {
            if (!scriptStart(scriptFile())) showToast("Start fehlgeschlagen");
        }
        break;

    case S_BLE:
        if (lv.nav(b)) break;
        if (b == BTN_BACK) go(S_MAIN);
        else if (b == BTN_SEL) {
            if (lv.sel == 0) go(S_BLE);
            else {
                bleSel = lv.sel - 1;
                go(S_BLE_DEV);
            }
        }
        break;

    case S_BLE_DEV:
        if (lv.nav(b)) break;
        if (b == BTN_BACK) {
            scr = S_BLE;
            buildBleList(bleSel + 1);
        }
        break;

    case S_NRF:
        if (lv.nav(b)) nrfSel = lv.sel;
        else if (b == BTN_BACK) go(S_MAIN);
        else if (b == BTN_SEL) {
            nrfSel = lv.sel;
            if (lv.sel == 0) go(S_NRF_SCAN);
            else if (lv.sel == 1) go(S_NRF_RX);
            else showToast(nrfDetect() ? "NRF24 erkannt" : "NRF24 nicht gefunden");
        }
        break;

    case S_NRF_SCAN:
        if (b == BTN_BACK) go(S_NRF);
        else if (b == BTN_UP) nrfCursor = (nrfCursor + 125) % 126;
        else if (b == BTN_DOWN) nrfCursor = (nrfCursor + 1) % 126;
        else if (b == BTN_SEL) {
            nrfPeakReset();
            showToast("Peaks gelöscht");
        }
        break;

    case S_NRF_RX:
        if (b == BTN_BACK) go(S_NRF);
        else if (b == BTN_UP) nrfRxSetChannel(nrfRxChannel() + 1);
        else if (b == BTN_DOWN) nrfRxSetChannel(nrfRxChannel() - 1);
        else if (b == BTN_SEL) nrfLog.clear();
        break;
    }
}

// ---------- Anzeige ----------
static String homeQrText() {
    if (apMode) return String("WIFI:S:" AP_SSID ";;");
    if (WiFi.status() == WL_CONNECTED) return "HTTP://" + WiFi.localIP().toString() + "/";
    return "";
}

// Gelbe Zone: y 0..15  |  Blaue Zone: y 16..63 (48 px)
static void renderHome() {
    String qrText = homeQrText();
    if (qrText.length() && qrFull) {
        // Vollbild: 2 Module Quiet-Zone, Module selbst komplett im blauen Bereich
        int sz = qrSizePx(qrText, 50, 2);
        drawQR(qrText, (128 - sz) / 2, 64 - sz, 50, 2);
        strlcpy(uiTitleBuf, "QR Vollbild", sizeof(uiTitleBuf));
        return;
    }

    if (apMode) {
        drawHeader("WLAN-Setup");
        drawQR(qrText, 128 - qrSizePx(qrText, 48), 16, 48);
        u8g2.setFont(u8g2_font_5x7_tf);
        u8g2.drawUTF8(0, 25, "1.QR scannen");
        u8g2.drawUTF8(0, 34, "2.Verbinden");
        u8g2.drawUTF8(0, 43, "3.WLAN eintr.");
        u8g2.drawUTF8(0, 54, "OK=Men\u00fc");
        u8g2.drawUTF8(0, 63, "Runter=Vollb.");
    } else if (WiFi.status() == WL_CONNECTED) {
        drawHeader("Web-UI");
        drawQR(qrText, 128 - qrSizePx(qrText, 48), 16, 48);
        u8g2.setFont(u8g2_font_5x7_tf);
        u8g2.drawUTF8(0, 25, "QR scannen");
        u8g2.drawUTF8(0, 35, WiFi.localIP().toString().c_str());
        u8g2.drawUTF8(0, 54, "OK=Men\u00fc");
        u8g2.drawUTF8(0, 63, "Runter=Vollb.");
    } else {
        drawHeader("ESP Multitool");
        u8g2.setFont(u8g2_font_6x10_tf);
        u8g2.drawUTF8(0, 32, "Kein WLAN");
        u8g2.drawUTF8(0, 46, "WLAN > Onboarding");
        u8g2.drawUTF8(0, 60, "OK = Men\u00fc");
    }
}

static void renderSpectrum() {
    char t[24];
    snprintf(t, sizeof(t), "Ch%d %dMHz", nrfCursor, 2400 + nrfCursor);
    drawHeader(t);
    for (int c = 0; c < 126; c++) {
        int x = c + 1;
        int h = nrfLevel[c] * 46 / 63;
        if (h) u8g2.drawVLine(x, 63 - h, h + 1);
        int ph = nrfPeak[c] * 46 / 63;
        if (ph) u8g2.drawPixel(x, 63 - ph);
    }
    int x = nrfCursor + 1;
    u8g2.drawTriangle(x - 2, 16, x + 2, 16, x, 19);
    for (int y = 21; y < 63; y += 4) u8g2.drawPixel(x, y);
}

static void renderNrfRx() {
    String hex;
    while (nrfRxPoll(hex)) {
        nrfLog.insert(nrfLog.begin(), hex);
        if (nrfLog.size() > 20) nrfLog.pop_back();
    }
    drawHeader("RX " + nrfRxInfo());
    u8g2.setFont(u8g2_font_5x7_tf);
    if (nrfLog.empty()) {
        u8g2.drawUTF8(0, 30, "Warte auf Pakete ...");
        u8g2.drawUTF8(0, 42, "Hoch/Runter: Kanal");
        u8g2.drawUTF8(0, 52, "Config: /nrf.cfg");
        return;
    }
    for (int i = 0; i < 5 && i < (int)nrfLog.size(); i++) u8g2.drawStr(0, 25 + i * 9, nrfLog[i].c_str());
}

static void renderScript() {
    bool run = scriptRunning();
    String name = scriptFile();
    int sl = name.lastIndexOf('/');
    drawHeader(String(run ? "> " : "") + name.substring(sl + 1));
    std::vector<String> lines;
    String err = run ? String("") : scriptError();
    if (err.length()) {
        lines.push_back("FEHLER:");
        wrapInto(lines, err, 21);
    } else {
        std::vector<String> scrn = scriptScreen();
        if (!scrn.empty()) lines = scrn;
        else {
            // letzte Zeilen der Serial-Ausgabe
            std::vector<String> all;
            wrapInto(all, scriptLog(), 21);
            while (!all.empty() && all.back().length() == 0) all.pop_back();
            for (size_t i = all.size() > 4 ? all.size() - 4 : 0; i < all.size(); i++) lines.push_back(all[i]);
        }
    }
    u8g2.setFont(u8g2_font_6x10_tf);
    int maxL = run ? 4 : 3;
    int start = (int)lines.size() > maxL ? (err.length() ? 0 : lines.size() - maxL) : 0;
    for (int i = 0; i < maxL && start + i < (int)lines.size(); i++) u8g2.drawUTF8(0, 26 + i * 12, lines[start + i].c_str());
    if (!run) {
        u8g2.setFont(u8g2_font_5x7_tf);
        u8g2.drawStr(0, 63, "OK=neu  Hoch+Runter=zur.");
    }
}

static void render() {
    u8g2.clearBuffer();
    switch (scr) {
    case S_HOME:
        renderHome();
        break;
    case S_IR_REC: {
        String s, l;
        if (irPoll(s, l)) {
            capSummary = s;
            capLine = l;
            go(S_IR_DONE);
            lv.draw();
        } else {
            drawHeader("IR Aufnahme");
            u8g2.setFont(u8g2_font_6x10_tf);
            u8g2.drawUTF8(0, 28, "Fernbedienung auf");
            u8g2.drawUTF8(0, 40, "Empfänger richten");
            u8g2.drawUTF8(0, 52, "und Taste drücken");
            u8g2.setFont(u8g2_font_5x7_tf);
            u8g2.drawUTF8(0, 63, "Hoch+Runter = Zurück");
        }
        break;
    }
    case S_NRF_SCAN:
        nrfScanSweep();
        renderSpectrum();
        break;
    case S_NRF_RX:
        renderNrfRx();
        break;
    case S_SCRIPT_RUN:
        renderScript();
        break;
    case S_STATUS:
    case S_WIFI_INFO:
        if (millis() - lastInfo > 1000) {
            lastInfo = millis();
            if (scr == S_STATUS) setInfo("Status", statusLines());
            else setInfo("WLAN Info", wifiLines());
        }
        lv.draw();
        break;
    default:
        lv.draw();
        break;
    }

    if (millis() < toastUntil) {
        u8g2.setFont(u8g2_font_6x10_tf);
        int w = min(124, (int)u8g2.getUTF8Width(toast.c_str()) + 8);
        int x = (128 - w) / 2;
        u8g2.setDrawColor(0);
        u8g2.drawBox(x - 1, 41, w + 2, 18);
        u8g2.setDrawColor(1);
        u8g2.drawRFrame(x, 42, w, 16, 3);
        u8g2.drawUTF8(x + 4, 54, toast.c_str());
    }
    uiFlush();
}

// ---------- Arduino ----------
void setup() {
    Serial.begin(115200);
    uiBegin();
    showMessage("ESP Multitool", "v" FW_VERSION, "Starte ...");
    if (!LittleFS.begin(true)) Serial.println("LittleFS Fehler");
    scriptBootCheck();
    irBegin();
    nrfDetect();
    netBegin();
    go(S_HOME);
    if (scriptBootNote().length()) showToast("Skript-Absturz! Web>Skript");
}

void loop() {
    netLoop();

    Btn b = readButtons();
    if (b == BTN_NONE) b = remotePop();

    if (pendIrFlag) {
        char nm[48];
        portENTER_CRITICAL(&rmux);
        strlcpy(nm, pendIr, sizeof(nm));
        pendIrFlag = false;
        portEXIT_CRITICAL(&rmux);
        String line = irGetLine(nm);
        showToast(line.length() && irSendLine(line) ? String("Gesendet: " + String(nm)) : String("IR-Fehler"));
    }
    if (pendRunFlag) {
        char pth[64];
        portENTER_CRITICAL(&rmux);
        strlcpy(pth, pendRun, sizeof(pth));
        pendRunFlag = false;
        portEXIT_CRITICAL(&rmux);
        if (scriptStart(String(pth))) go(S_SCRIPT_RUN);
    }
    if (pendNrf) {
        pendNrf = false;
        if (scr != S_NRF_SCAN) go(S_NRF_SCAN);
    }

    handle(b);
    render();
    if (scr != S_NRF_SCAN) delay(10);
}
