## ESP Multitool 1.1.0

Firmware für **ESP32-D0WD + SSD1306-OLED (128×64)**.

### Neu
- **`.ino`-Sketches direkt auf dem Gerät ausführen** – im Web-UI (Tab *Skript*) schreiben oder hochladen und starten, ohne Kompilieren/Neuflashen. Unterstützt eine Arduino-C++-Teilmenge: `setup()`/`loop()`, Funktionen, `int/long/bool`, Arrays, `#define`, `if/for/while`, `pinMode`, `digitalWrite/Read`, `analogRead/Write`, `delay`, `millis`, `Serial.print`, `oled.print`, `irSend("Name")`.
- OLED-Menü *Skripte (.ino)* mit Laufbildschirm (Hoch+Runter = Stop)
- **Datei-Upload repariert**: Platz-Check, echte Fehlermeldungen, kein Watchdog-Reset mehr beim Schreiben

### Dateien
| Datei | Wofür |
|---|---|
| `esp-multitool-full.bin` | Erstinstallation per USB, Offset `0x0` (Bootloader + Partitionen + App) |
| `esp-multitool-app.bin` | **OTA-Update** über das Web-UI → Tab *Firmware* |

### Flashen
- **Browser:** https://real3rr0r303yt.github.io/bruce-d0wd/ → *Install ESP Multitool* (Chrome/Edge)
- **USB:** `esptool --chip esp32 --port <PORT> --baud 460800 write-flash 0x0 esp-multitool-full.bin`
- **OTA:** Web-UI → *Firmware* → `esp-multitool-app.bin`
