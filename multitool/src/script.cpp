// Fuehrt hochgeladene .ino-Dateien mit dem Mini-Interpreter in einem eigenen Task aus.
#include "ino_interp.h"
#include "shared.h"
#include <LittleFS.h>

static SemaphoreHandle_t mtx = nullptr;
static String logBuf, errBuf, curFile;
static std::vector<String> scrLines;
static String scrCur;
static volatile bool stopReq = false, running = false;

static const size_t LOG_MAX = 4096;
static const size_t SRC_MAX = 32768;

static void lock() {
    if (!mtx) mtx = xSemaphoreCreateMutex();
    xSemaphoreTake(mtx, portMAX_DELAY);
}
static void unlock() { xSemaphoreGive(mtx); }

static void logAppend(const String &s) {
    lock();
    logBuf += s;
    if (logBuf.length() > LOG_MAX) logBuf.remove(0, logBuf.length() - LOG_MAX);
    unlock();
    Serial.print(s);
}

// ---------- Pin-Regeln ----------
// Belegt: OLED 21/22, NRF24 4/5/18/19/23, Flash 6-11, Taster 14/32/33 (nur lesen)
static bool inList(long p, const int *l, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (l[i] == p) return true;
    return false;
}
static const int OUT_OK[] = {2, 12, 13, 15, 16, 17, 25, 26, 27};
static const int IN_OK[] = {2, 12, 13, 14, 15, 16, 17, 25, 26, 27, 32, 33, 34, 35, 36, 39};

static bool pinErr(long p, bool out, std::string &err) {
    err = "GPIO " + std::to_string(p) + (out ? " nicht als Ausgang erlaubt" : " nicht als Eingang erlaubt") +
          ". Frei: 2,12,13,15,16,17,25,26,27" + (out ? "" : " (+ lesen: 14,32-39)");
    return false;
}

class EspHal : public InoHal {
  public:
    void serialOut(const std::string &s) override { logAppend(String(s.c_str())); }
    void oledOut(const std::string &s) override {
        lock();
        for (char c : s) {
            if (c == '\n') {
                scrLines.push_back(scrCur);
                scrCur = "";
                if (scrLines.size() > 4) scrLines.erase(scrLines.begin());
            } else if (scrCur.length() < 40) scrCur += c;
        }
        unlock();
    }
    void oledClear() override {
        lock();
        scrLines.clear();
        scrCur = "";
        unlock();
    }
    bool pinMode(long p, long m, std::string &err) override {
        bool out = (m == 1);
        if (!inList(p, out ? OUT_OK : IN_OK, out ? sizeof(OUT_OK) / sizeof(int) : sizeof(IN_OK) / sizeof(int)))
            return pinErr(p, out, err);
        static const uint8_t MODES[] = {INPUT, OUTPUT, INPUT_PULLUP, INPUT_PULLDOWN};
        if (m < 0 || m > 3) {
            err = "Unbekannter pinMode";
            return false;
        }
        if (p == PIN_BTN_SEL || p == PIN_BTN_UP || p == PIN_BTN_DOWN) return true;  // Taster bleiben PULLUP
        ::pinMode(p, MODES[m]);
        return true;
    }
    bool digitalWrite(long p, long v, std::string &err) override {
        if (!inList(p, OUT_OK, sizeof(OUT_OK) / sizeof(int))) return pinErr(p, true, err);
        ::digitalWrite(p, v ? HIGH : LOW);
        return true;
    }
    bool digitalRead(long p, long &v, std::string &err) override {
        if (!inList(p, IN_OK, sizeof(IN_OK) / sizeof(int))) return pinErr(p, false, err);
        v = ::digitalRead(p);
        return true;
    }
    bool analogRead(long p, long &v, std::string &err) override {
        if (p < 32 || p > 39 || p == 37 || p == 38) {
            err = "analogRead nur auf GPIO 32-36, 39 (ADC1)";
            return false;
        }
        v = ::analogRead(p);
        return true;
    }
    bool analogWrite(long p, long v, std::string &err) override {
        if (!inList(p, OUT_OK, sizeof(OUT_OK) / sizeof(int))) return pinErr(p, true, err);
        ::analogWrite(p, constrain(v, 0, 255));
        return true;
    }
    bool irSend(const std::string &name, std::string &err) override {
        String line = irGetLine(String(name.c_str()));
        if (!line.length()) {
            err = "IR-Code '" + name + "' nicht gespeichert";
            return false;
        }
        if (!irSendLine(line)) {
            err = "IR-Code '" + name + "' konnte nicht gesendet werden";
            return false;
        }
        return true;
    }
    void delayMs(long ms) override {
        while (ms > 0 && !stopReq) {
            long d = ms > 20 ? 20 : ms;
            vTaskDelay(pdMS_TO_TICKS(d) ? pdMS_TO_TICKS(d) : 1);
            ms -= d;
        }
    }
    void delayUs(long us) override {
        if (us <= 5000) ::delayMicroseconds(us);
        else delayMs(us / 1000);
    }
    long millis() override { return (long)::millis(); }
    long micros() override { return (long)::micros(); }
    long random(long lo, long hi) override { return hi > lo ? ::random(lo, hi) : lo; }
    bool stopRequested() override { return stopReq; }
    void yield() override { vTaskDelay(1); }
};

static void taskFn(void *) {
    String path = curFile;
    std::string src;
    File f = LittleFS.open(path, "r");
    bool ok = false;
    String msg;
    if (!f) msg = "Datei nicht lesbar";
    else if (f.size() > SRC_MAX) msg = "Datei zu gross (max 32 kB)";
    else {
        src.reserve(f.size());
        char buf[256];
        while (f.available()) {
            int n = f.read((uint8_t *)buf, sizeof(buf));
            if (n <= 0) break;
            src.append(buf, n);
        }
    }
    if (f) f.close();

    if (!msg.length()) {
        InoInterp *I = new InoInterp();
        EspHal hal;
        if (!I->load(src)) msg = I->errorMsg.c_str();
        else {
            logAppend("> Start " + path + "\n");
            ok = I->run(hal);
            if (!ok) msg = I->errorMsg.c_str();
        }
        delete I;
    }
    if (ok) logAppend(stopReq ? "\n[gestoppt]\n" : "\n[beendet]\n");
    else {
        logAppend("\n[FEHLER] " + msg + "\n");
        lock();
        errBuf = msg;
        unlock();
    }
    running = false;
    vTaskDelete(nullptr);
}

bool scriptStart(const String &path) {
    if (running) return false;
    if (!LittleFS.exists(path)) return false;
    lock();
    logBuf = "";
    errBuf = "";
    scrLines.clear();
    scrCur = "";
    curFile = path;
    unlock();
    stopReq = false;
    running = true;
    if (xTaskCreatePinnedToCore(taskFn, "ino", 32768, nullptr, 1, nullptr, 1) != pdPASS) {
        running = false;
        logAppend("[FEHLER] Zu wenig Speicher fuer Skript-Task\n");
        return false;
    }
    return true;
}

void scriptStop() { stopReq = true; }
bool scriptRunning() { return running; }

String scriptFile() {
    lock();
    String s = curFile;
    unlock();
    return s;
}
String scriptLog() {
    lock();
    String s = logBuf;
    unlock();
    return s;
}
String scriptError() {
    lock();
    String s = errBuf;
    unlock();
    return s;
}
std::vector<String> scriptScreen() {
    lock();
    std::vector<String> v = scrLines;
    if (scrCur.length()) v.push_back(scrCur);
    unlock();
    while (v.size() > 4) v.erase(v.begin());
    return v;
}
