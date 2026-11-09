#include "shared.h"
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRutils.h>
#include <LittleFS.h>

// Dateiformat /ir_codes.txt, eine Zeile pro Code:
//   Name|PROTOKOLL|Bits|HexWert        (z.B. TV_An|NEC|32|20DF10EF)
//   Name|PROTOKOLL|Bits|HexBytes       (Klima-Protokolle mit State)
//   Name|RAW|38|9000,4500,560,...       (Rohdaten in us, Traeger in kHz)

static IRrecv irrecv(PIN_IR_RX, 1024, 50, true);
static IRsend irsend(PIN_IR_TX);
static decode_results results;
static bool rxOn = false;

void irBegin() { irsend.begin(); }

void irRxStart() {
    if (!rxOn) {
        irrecv.enableIRIn();
        rxOn = true;
    }
}

void irRxStop() {
    if (rxOn) {
        irrecv.disableIRIn();
        rxOn = false;
    }
}

bool irPoll(String &summary, String &line) {
    if (!rxOn || !irrecv.decode(&results)) return false;
    decode_type_t t = results.decode_type;

    if (results.repeat) {  // NEC-Wiederholungscode ignorieren
        irrecv.resume();
        return false;
    }

    if (t == UNKNOWN) {
        uint16_t len = getCorrectedRawLength(&results);
        if (len < 12) {  // Stoerimpuls
            irrecv.resume();
            return false;
        }
        uint16_t *raw = resultToRawArray(&results);
        line = "RAW|38|";
        for (uint16_t i = 0; i < len; i++) {
            if (i) line += ',';
            line += String(raw[i]);
        }
        delete[] raw;
        summary = "RAW " + String(len);
    } else if (hasACState(t)) {
        uint16_t n = results.bits / 8;
        line = typeToString(t) + "|" + String(results.bits) + "|";
        char h[3];
        for (uint16_t i = 0; i < n; i++) {
            snprintf(h, sizeof(h), "%02X", results.state[i]);
            line += h;
        }
        summary = typeToString(t) + " " + String(results.bits) + "b";
    } else {
        line = typeToString(t) + "|" + String(results.bits) + "|" + uint64ToString(results.value, 16);
        summary = typeToString(t) + " " + String(results.bits) + "b";
    }
    irrecv.resume();
    return true;
}

bool irSendLine(const String &line) {
    int a = line.indexOf('|');
    int b = line.indexOf('|', a + 1);
    if (a < 0 || b < 0) return false;
    String proto = line.substring(0, a);
    String p2 = line.substring(a + 1, b);
    String data = line.substring(b + 1);
    data.trim();

    bool wasRx = rxOn;
    irRxStop();
    bool ok = false;

    if (proto == "RAW") {
        uint16_t khz = p2.toInt();
        if (!khz) khz = 38;
        std::vector<uint16_t> t;
        t.reserve(256);
        int s = 0;
        int L = data.length();
        while (s < L) {
            int c = data.indexOf(',', s);
            if (c < 0) c = L;
            t.push_back((uint16_t)data.substring(s, c).toInt());
            s = c + 1;
        }
        if (!t.empty()) {
            irsend.sendRaw(t.data(), (uint16_t)t.size(), khz);
            ok = true;
        }
    } else {
        decode_type_t t = strToDecodeType(proto.c_str());
        uint16_t bits = p2.toInt();
        if (hasACState(t)) {
            uint8_t st[kStateSizeMax];
            uint16_t n = 0;
            for (int i = 0; i + 1 < (int)data.length() && n < kStateSizeMax; i += 2)
                st[n++] = strtoul(data.substring(i, i + 2).c_str(), nullptr, 16);
            ok = irsend.send(t, st, n);
        } else {
            uint64_t v = strtoull(data.c_str(), nullptr, 16);
            ok = irsend.send(t, v, bits);
        }
    }
    if (wasRx) irRxStart();
    return ok;
}

// ---------- Speicher ----------
static std::vector<String> readLines() {
    std::vector<String> v;
    if (!LittleFS.exists(IR_FILE)) return v;
    File f = LittleFS.open(IR_FILE, "r");
    if (!f) return v;
    while (f.available()) {
        String l = f.readStringUntil('\n');
        l.trim();
        if (l.length() && l[0] != '#' && l.indexOf('|') > 0) v.push_back(l);
    }
    f.close();
    return v;
}

static bool writeLines(const std::vector<String> &v) {
    File f = LittleFS.open(IR_FILE, "w");
    if (!f) return false;
    for (auto &l : v) f.println(l);
    f.close();
    return true;
}

std::vector<String> irLoadNames() {
    std::vector<String> names;
    for (auto &l : readLines()) names.push_back(l.substring(0, l.indexOf('|')));
    return names;
}

String irGetLine(const String &name) {
    for (auto &l : readLines()) {
        int p = l.indexOf('|');
        if (l.substring(0, p) == name) return l.substring(p + 1);
    }
    return "";
}

bool irSave(const String &name, const String &line) {
    File f = LittleFS.open(IR_FILE, FILE_APPEND);
    if (!f) return false;
    f.print(name);
    f.print('|');
    f.println(line);
    f.close();
    return true;
}

bool irDelete(const String &name) {
    auto v = readLines();
    std::vector<String> out;
    for (auto &l : v)
        if (l.substring(0, l.indexOf('|')) != name) out.push_back(l);
    return writeLines(out);
}

bool irRename(const String &from, const String &to) {
    String clean = to;
    clean.replace("|", "_");
    clean.trim();
    if (!clean.length()) return false;
    auto v = readLines();
    for (auto &l : v) {
        int p = l.indexOf('|');
        if (l.substring(0, p) == from) l = clean + l.substring(p);
    }
    return writeLines(v);
}

String irNextName() {
    auto names = irLoadNames();
    for (int i = 1; i < 1000; i++) {
        char b[12];
        snprintf(b, sizeof(b), "Code%02d", i);
        bool used = false;
        for (auto &n : names)
            if (n == b) used = true;
        if (!used) return String(b);
    }
    return "Code";
}
