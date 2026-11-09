#include "shared.h"
#include <LittleFS.h>
#include <NimBLEDevice.h>
#include <RF24.h>
#include <SPI.h>
#include <algorithm>

// ================= NRF24L01 (nur Empfang / Messung) =================
static RF24 radio(PIN_NRF_CE, PIN_NRF_CSN);
bool nrfPresent = false;
uint8_t nrfLevel[126];
uint8_t nrfPeak[126];
volatile bool nrfScanning = false;

struct NrfCfg {
    int ch = 76;
    char addr[6] = "1Node";
    int rate = 1000;  // 250 | 1000 | 2000 kbps
    int size = 4;     // 0 = dynamische Payload
};
static NrfCfg cfg;
static bool rxActive = false;

bool nrfDetect() {
    nrfPresent = radio.begin() && radio.isChipConnected();
    if (nrfPresent) radio.powerDown();
    return nrfPresent;
}

bool nrfScanStart() {
    if (!radio.begin() || !radio.isChipConnected()) {
        nrfPresent = false;
        return false;
    }
    nrfPresent = true;
    radio.setAutoAck(false);
    radio.setDataRate(RF24_1MBPS);
    radio.startListening();
    radio.stopListening();
    memset(nrfLevel, 0, sizeof(nrfLevel));
    memset(nrfPeak, 0, sizeof(nrfPeak));
    nrfScanning = true;
    return true;
}

// Ein Durchlauf ueber alle 126 Kanaele (RPD = Signal > -64 dBm)
void nrfScanSweep() {
    if (!nrfScanning) return;
    static uint8_t hits[126];
    const int reps = 2;
    memset(hits, 0, sizeof(hits));
    for (int r = 0; r < reps; r++) {
        for (int ch = 0; ch < 126; ch++) {
            radio.setChannel(ch);
            radio.startListening();
            delayMicroseconds(140);
            if (radio.testRPD()) hits[ch]++;
            radio.stopListening();
        }
    }
    for (int ch = 0; ch < 126; ch++) {
        uint8_t tgt = hits[ch] * 63 / reps;
        if (tgt > nrfLevel[ch]) nrfLevel[ch] = tgt;
        else nrfLevel[ch] = nrfLevel[ch] > 4 ? nrfLevel[ch] - 4 : 0;
        if (nrfLevel[ch] > nrfPeak[ch]) nrfPeak[ch] = nrfLevel[ch];
    }
}

void nrfPeakReset() { memset(nrfPeak, 0, sizeof(nrfPeak)); }

// /nrf.cfg (optional):  ch=76  addr=1Node  rate=1000  size=4
static void loadCfg() {
    if (!LittleFS.exists(NRF_CFG)) return;
    File f = LittleFS.open(NRF_CFG, "r");
    if (!f) return;
    while (f.available()) {
        String l = f.readStringUntil('\n');
        l.trim();
        int e = l.indexOf('=');
        if (e < 1) continue;
        String k = l.substring(0, e), v = l.substring(e + 1);
        k.trim();
        v.trim();
        if (k == "ch") cfg.ch = constrain(v.toInt(), 0, 125);
        else if (k == "addr" && v.length() == 5) strlcpy(cfg.addr, v.c_str(), sizeof(cfg.addr));
        else if (k == "rate") cfg.rate = v.toInt();
        else if (k == "size") cfg.size = constrain(v.toInt(), 0, 32);
    }
    f.close();
}

static void applyRx() {
    radio.stopListening();
    radio.setChannel(cfg.ch);
    radio.setDataRate(cfg.rate == 250 ? RF24_250KBPS : (cfg.rate == 2000 ? RF24_2MBPS : RF24_1MBPS));
    radio.setPALevel(RF24_PA_LOW);
    if (cfg.size == 0) radio.enableDynamicPayloads();
    else {
        radio.disableDynamicPayloads();
        radio.setPayloadSize(cfg.size);
    }
    radio.openReadingPipe(1, (const uint8_t *)cfg.addr);
    radio.startListening();
}

bool nrfRxStart() {
    loadCfg();
    if (!radio.begin() || !radio.isChipConnected()) {
        nrfPresent = false;
        return false;
    }
    nrfPresent = true;
    applyRx();
    rxActive = true;
    return true;
}

bool nrfRxPoll(String &hex) {
    if (!rxActive || !radio.available()) return false;
    uint8_t len = cfg.size ? cfg.size : radio.getDynamicPayloadSize();
    if (len == 0 || len > 32) {
        radio.flush_rx();
        return false;
    }
    uint8_t buf[32];
    radio.read(buf, len);
    hex = "";
    char h[4];
    for (uint8_t i = 0; i < len; i++) {
        snprintf(h, sizeof(h), "%02X", buf[i]);
        hex += h;
        if (i < len - 1 && (i % 2) == 1) hex += ' ';
    }
    return true;
}

void nrfRxSetChannel(int ch) {
    cfg.ch = (ch + 126) % 126;
    if (rxActive) applyRx();
}

int nrfRxChannel() { return cfg.ch; }

String nrfRxInfo() { return "Ch" + String(cfg.ch) + " " + String(cfg.addr); }

void nrfStop() {
    nrfScanning = false;
    rxActive = false;
    if (nrfPresent) {
        radio.stopListening();
        radio.powerDown();
    }
}

// ================= BLE (nur passiver/aktiver Scan) =================
std::vector<BleDev> bleScan(uint32_t seconds) {
    static bool inited = false;
    if (!inited) {
        NimBLEDevice::init("");
        inited = true;
    }
    NimBLEScan *s = NimBLEDevice::getScan();
    s->setActiveScan(true);
    s->setInterval(100);
    s->setWindow(99);
    NimBLEScanResults r = s->start(seconds, false);
    std::vector<BleDev> v;
    for (int i = 0; i < r.getCount(); i++) {
        NimBLEAdvertisedDevice d = r.getDevice(i);
        BleDev b;
        b.name = d.haveName() ? String(d.getName().c_str()) : String("");
        b.mac = String(d.getAddress().toString().c_str());
        b.rssi = d.getRSSI();
        v.push_back(b);
    }
    s->clearResults();
    std::sort(v.begin(), v.end(), [](const BleDev &a, const BleDev &b) { return a.rssi > b.rssi; });
    return v;
}
