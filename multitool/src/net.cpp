#include "shared.h"
#include "webpage.h"
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>

bool apMode = false;
static AsyncWebServer server(80);
static DNSServer dns;
static volatile uint32_t rebootAt = 0;
static File upFile;
static bool upOk = false;
static String upName, upErr;
static size_t upBytes = 0;

// ---------- Hilfen ----------
String fmtBytes(size_t b) {
    if (b < 1024) return String(b) + " B";
    if (b < 1048576) return String(b / 1024.0, 1) + " kB";
    return String(b / 1048576.0, 2) + " MB";
}

static String jsonEsc(const String &s) {
    String o;
    o.reserve(s.length() + 4);
    for (unsigned i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') {
            o += '\\';
            o += c;
        } else if ((uint8_t)c < 0x20) o += ' ';
        else o += c;
    }
    return o;
}

static String pref(const char *k) {
    Preferences p;
    String v;
    if (p.begin("wifi", true)) {
        v = p.getString(k, "");
        p.end();
    }
    return v;
}

static String qp(AsyncWebServerRequest *r, const char *name) {
    if (r->hasParam(name)) return r->getParam(name)->value();
    if (r->hasParam(name, true)) return r->getParam(name, true)->value();
    return "";
}

static void listDir(const char *path, std::vector<String> &names, std::vector<size_t> &sizes) {
    File d = LittleFS.open(path);
    if (!d || !d.isDirectory()) return;
    File f = d.openNextFile();
    while (f) {
        if (f.isDirectory()) listDir(f.path(), names, sizes);
        else {
            names.push_back(String(f.path()));
            sizes.push_back(f.size());
        }
        f = d.openNextFile();
    }
}

void fsListAll(std::vector<String> &names, std::vector<size_t> &sizes) {
    names.clear();
    sizes.clear();
    listDir("/", names, sizes);
}

// ---------- WLAN ----------
bool netHasSaved() { return pref("ssid").length() > 0; }
String netSavedSsid() { return pref("ssid"); }
bool netConnected() { return !apMode && WiFi.status() == WL_CONNECTED; }
String netIP() { return apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString(); }

bool netConnectSaved(uint32_t timeoutMs) {
    String s = pref("ssid"), p = pref("pass");
    if (!s.length()) return false;
    if (apMode) {
        dns.stop();
        WiFi.softAPdisconnect(true);
        apMode = false;
    }
    WiFi.setHostname(HOSTNAME);
    WiFi.mode(WIFI_STA);
    WiFi.begin(s.c_str(), p.c_str());
    uint32_t t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < timeoutMs) delay(100);
    if (WiFi.status() == WL_CONNECTED) {
        MDNS.end();
        if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
        return true;
    }
    return false;
}

void netStartAP() {
    WiFi.disconnect();
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID);
    delay(100);
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dns.start(53, "*", WiFi.softAPIP());
    apMode = true;
}

void netResetWifi() {
    Preferences p;
    if (p.begin("wifi", false)) {
        p.clear();
        p.end();
    }
    WiFi.disconnect(false, true);
    netStartAP();
}

// ---------- Webserver ----------
static void setupRoutes() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) {
        r->send(200, "text/html", apMode ? PORTAL_HTML : APP_HTML);
    });
    server.on("/app", HTTP_GET, [](AsyncWebServerRequest *r) { r->send(200, "text/html", APP_HTML); });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *r) {
        String j = "{";
        j += "\"mode\":\"" + String(apMode ? "AP" : (WiFi.status() == WL_CONNECTED ? "STA" : "OFF")) + "\",";
        j += "\"ssid\":\"" + jsonEsc(apMode ? String(AP_SSID) : WiFi.SSID()) + "\",";
        j += "\"ip\":\"" + netIP() + "\",";
        j += "\"rssi\":" + String(apMode ? 0 : WiFi.RSSI()) + ",";
        j += "\"up\":" + String(millis() / 1000) + ",";
        j += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
        j += "\"fsT\":" + String(LittleFS.totalBytes()) + ",";
        j += "\"fsU\":" + String(LittleFS.usedBytes()) + ",";
        j += "\"nrf\":" + String(nrfPresent ? "true" : "false") + ",";
        j += "\"oled\":" + String(oledOk ? "true" : "false") + ",";
        j += "\"scr\":\"" + jsonEsc(String(uiTitleBuf)) + "\",";
        j += "\"fw\":\"" FW_VERSION "\"}";
        r->send(200, "application/json", j);
    });

    server.on("/api/screen", HTTP_GET, [](AsyncWebServerRequest *r) {
        AsyncResponseStream *s = r->beginResponseStream("application/octet-stream");
        s->write(screenMirror, sizeof(screenMirror));
        r->send(s);
    });

    server.on("/api/btn", HTTP_POST, [](AsyncWebServerRequest *r) {
        String b = qp(r, "b");
        Btn x = BTN_NONE;
        if (b == "up") x = BTN_UP;
        else if (b == "down") x = BTN_DOWN;
        else if (b == "sel") x = BTN_SEL;
        else if (b == "back") x = BTN_BACK;
        if (x != BTN_NONE) remotePushBtn(x);
        r->send(200, "text/plain", "OK");
    });

    // ---- IR ----
    server.on("/api/irlist", HTTP_GET, [](AsyncWebServerRequest *r) {
        auto v = irLoadNames();
        String j = "[";
        for (size_t i = 0; i < v.size(); i++) {
            if (i) j += ",";
            j += "\"" + jsonEsc(v[i]) + "\"";
        }
        j += "]";
        r->send(200, "application/json", j);
    });
    server.on("/api/irsend", HTTP_POST, [](AsyncWebServerRequest *r) {
        remoteIrSend(qp(r, "name"));
        r->send(200, "text/plain", "OK");
    });
    server.on("/api/irdel", HTTP_POST, [](AsyncWebServerRequest *r) {
        irDelete(qp(r, "name"));
        r->send(200, "text/plain", "OK");
    });
    server.on("/api/irren", HTTP_POST, [](AsyncWebServerRequest *r) {
        irRename(qp(r, "name"), qp(r, "to"));
        r->send(200, "text/plain", "OK");
    });

    // ---- NRF24 ----
    server.on("/api/nrf", HTTP_GET, [](AsyncWebServerRequest *r) {
        String j = "{\"on\":" + String(nrfScanning ? "true" : "false") + ",\"lv\":[";
        for (int i = 0; i < 126; i++) {
            if (i) j += ",";
            j += String(nrfLevel[i]);
        }
        j += "],\"pk\":[";
        for (int i = 0; i < 126; i++) {
            if (i) j += ",";
            j += String(nrfPeak[i]);
        }
        j += "]}";
        r->send(200, "application/json", j);
    });
    server.on("/api/nrfstart", HTTP_POST, [](AsyncWebServerRequest *r) {
        remoteNrfScan();
        r->send(200, "text/plain", "OK");
    });

    // ---- Dateien ----
    server.on("/api/files", HTTP_GET, [](AsyncWebServerRequest *r) {
        std::vector<String> n;
        std::vector<size_t> s;
        fsListAll(n, s);
        String j = "[";
        for (size_t i = 0; i < n.size(); i++) {
            if (i) j += ",";
            j += "{\"n\":\"" + jsonEsc(n[i]) + "\",\"s\":" + String(s[i]) + "}";
        }
        j += "]";
        r->send(200, "application/json", j);
    });
    server.on("/api/dl", HTTP_GET, [](AsyncWebServerRequest *r) {
        String f = qp(r, "f");
        if (!f.startsWith("/") || !LittleFS.exists(f)) {
            r->send(404, "text/plain", "Nicht gefunden");
            return;
        }
        r->send(LittleFS, f, "application/octet-stream", true);
    });
    server.on("/api/fdel", HTTP_POST, [](AsyncWebServerRequest *r) {
        String f = qp(r, "f");
        bool ok = f.startsWith("/") && LittleFS.remove(f);
        r->send(ok ? 200 : 400, "text/plain", ok ? "OK" : "Fehler");
    });
    server.on(
        "/api/upload", HTTP_POST,
        [](AsyncWebServerRequest *r) {
            String msg;
            if (upOk) msg = "OK: " + upName + " (" + fmtBytes(upBytes) + ")";
            else msg = "FEHLER: " + (upErr.length() ? upErr : String("Upload unvollständig"));
            r->send(upOk ? 200 : 500, "text/plain", msg);
        },
        [](AsyncWebServerRequest *r, const String &fn, size_t index, uint8_t *data, size_t len, bool final) {
            if (index == 0) {
                upOk = true;
                upErr = "";
                upBytes = 0;
                String name = fn;
                name.replace("/", "");
                name.replace("\\", "");
                name.replace("..", "");
                name.trim();
                if (!name.length()) name = "upload.bin";
                if (name.length() > 48) name = name.substring(name.length() - 48);
                upName = "/" + name;
                if (upFile) upFile.close();
                size_t freeB = LittleFS.totalBytes() - LittleFS.usedBytes();
                size_t need = r->contentLength();
                if (LittleFS.exists(upName)) {
                    File old = LittleFS.open(upName, "r");
                    if (old) {
                        freeB += old.size();
                        old.close();
                    }
                }
                if (need > freeB) {
                    upOk = false;
                    upErr = "Zu groß: " + fmtBytes(need) + ", frei " + fmtBytes(freeB);
                } else {
                    upFile = LittleFS.open(upName, FILE_WRITE);
                    if (!upFile) {
                        upOk = false;
                        upErr = "Datei kann nicht angelegt werden";
                    }
                }
                Serial.printf("[upload] %s start (%u B)\n", upName.c_str(), (unsigned)need);
            }
            if (upOk && upFile && len) {
                if (upFile.write(data, len) != len) {
                    upOk = false;
                    upErr = "Schreibfehler (Speicher voll?)";
                } else upBytes += len;
            }
            if (final) {
                if (upFile) upFile.close();
                if (!upOk) LittleFS.remove(upName);
                Serial.printf("[upload] %s %s (%u B)\n", upName.c_str(), upOk ? "ok" : upErr.c_str(), (unsigned)upBytes);
            }
        });

    // ---- OTA ----
    server.on(
        "/api/ota", HTTP_POST,
        [](AsyncWebServerRequest *r) {
            bool ok = !Update.hasError();
            AsyncWebServerResponse *resp =
                r->beginResponse(200, "text/plain", ok ? "OK - Neustart..." : "FEHLER: Update fehlgeschlagen");
            resp->addHeader("Connection", "close");
            r->send(resp);
            if (ok) rebootAt = millis() + 1500;
        },
        [](AsyncWebServerRequest *r, const String &fn, size_t index, uint8_t *data, size_t len, bool final) {
            if (index == 0) {
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Serial);
            }
            if (Update.isRunning() && Update.write(data, len) != len) Update.printError(Serial);
            if (final) {
                if (Update.end(true)) Serial.printf("OTA ok: %u Bytes\n", (unsigned)(index + len));
                else Update.printError(Serial);
            }
        });

    // ---- WLAN ----
    server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *r) {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_FAILED) {
            WiFi.scanNetworks(true);
            r->send(200, "application/json", "{\"busy\":true}");
            return;
        }
        if (n == WIFI_SCAN_RUNNING) {
            r->send(200, "application/json", "{\"busy\":true}");
            return;
        }
        String j = "{\"nets\":[";
        for (int i = 0; i < n; i++) {
            if (i) j += ",";
            j += "{\"s\":\"" + jsonEsc(WiFi.SSID(i)) + "\",\"r\":" + String(WiFi.RSSI(i)) +
                 ",\"o\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
        }
        j += "]}";
        WiFi.scanDelete();
        r->send(200, "application/json", j);
    });
    server.on("/api/wifisave", HTTP_POST, [](AsyncWebServerRequest *r) {
        String s = qp(r, "ssid"), p = qp(r, "pass");
        if (!s.length()) {
            r->send(400, "text/plain", "SSID fehlt");
            return;
        }
        Preferences pr;
        if (pr.begin("wifi", false)) {
            pr.putString("ssid", s);
            pr.putString("pass", p);
            pr.end();
        }
        r->send(200, "text/plain", "OK");
        rebootAt = millis() + 1500;
    });
    server.on("/api/wifireset", HTTP_POST, [](AsyncWebServerRequest *r) {
        Preferences pr;
        if (pr.begin("wifi", false)) {
            pr.clear();
            pr.end();
        }
        r->send(200, "text/plain", "OK");
        rebootAt = millis() + 1500;
    });

    // Captive Portal: im AP-Modus alles auf die Startseite umleiten
    server.onNotFound([](AsyncWebServerRequest *r) {
        if (apMode) {
            String u = "http://" + WiFi.softAPIP().toString() + "/";
            r->redirect(u.c_str());
        }
        else r->send(404, "text/plain", "Nicht gefunden");
    });
}

void netBegin() {
    WiFi.persistent(false);
    bool ok = false;
    if (netHasSaved()) {
        showMessage("WLAN", "Verbinde mit", netSavedSsid());
        ok = netConnectSaved(15000);
    }
    if (!ok) netStartAP();
    setupRoutes();
    server.begin();
}

void netLoop() {
    if (apMode) dns.processNextRequest();
    if (rebootAt && millis() > rebootAt) {
        delay(100);
        ESP.restart();
    }
}
