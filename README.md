# Bruce-D0WD

Open-source **hardware bring-up firmware** for the **ESP32-D0WD** (the classic
dual-core ESP32) paired with an **SSD1306 OLED** display. Inspired by the
Bruce multi-tool firmware and rebuilt as a clean, standard-UI scaffold.

On boot the firmware initializes the OLED and probes each wireless front-end —
**Wi-Fi, Bluetooth, NRF24, CC1101, IR, RFID** — then presents a simple,
button-driven menu that reports which modules are wired up and responding.

> **Scope & ethics.** This project is a *scaffold*: it initializes hardware and
> reports module presence. It intentionally ships **no transmit or attack
> functionality**. It is provided for **education, hobbyist RF experimentation,
> and authorized testing only**. Radio transmission is regulated — you are
> solely responsible for complying with the laws and regulations of your
> jurisdiction and for only testing systems you own or are explicitly
> authorized to test.

---

## Features

- SSD1306 128×64 OLED UI (I2C) with a title-bar menu and status pages
- One-pass boot scan that detects each peripheral on the shared SPI bus
- Per-module info pages (Wi-Fi MAC, bus/pin summary, detection status)
- Three-button navigation (Up / Down / Select)
- Browser flashing via [ESP Web Tools](https://esphome.github.io/esp-web-tools/)
- Clean modular source layout you can extend

## Hardware pinout

| Peripheral   | Signal              | ESP32 GPIO  |
|--------------|---------------------|-------------|
| OLED (I2C)   | SDA                 | 21          |
| OLED (I2C)   | SCL                 | 22          |
| SPI bus      | SCK                 | 18          |
| SPI bus      | MISO                | 19          |
| SPI bus      | MOSI                | 23          |
| NRF24L01     | CE / CSN            | 4 / 5       |
| CC1101       | CSN / GDO0 / GDO2   | 27 / 2 / 25 |
| RC522 RFID   | CS / RST            | 15 / 0      |
| Infrared     | TX / RX             | 12 / 13     |
| Buttons      | Up / Down / Select  | 32 / 33 / 14|

All SPI front-ends share a single VSPI bus (SCK/MISO/MOSI) with an individual
chip-select line per device. The OLED default I2C address is `0x3C` (some
panels use `0x3D`). Adjust everything in [`src/config.h`](src/config.h) to match
your own wiring.

## Repository layout

```
bruce-d0wd/
├── index.html          # GitHub Pages landing page + browser installer
├── manifest.json       # ESP Web Tools manifest
├── platformio.ini      # Build configuration + library dependencies
├── firmware/           # Place the merged installer binary here
├── src/
│   ├── config.h        # Pin map & build options
│   ├── main.cpp        # Boot, scan, menu loop
│   ├── display.h/.cpp  # SSD1306 rendering helpers
│   └── modules.h/.cpp  # Wireless front-end bring-up & detection
├── LICENSE             # GNU GPL v3.0
└── README.md
```

## Build from source

1. Install [PlatformIO](https://platformio.org/) (CLI or the VS Code extension).
2. Clone and build:
   ```bash
   git clone https://github.com/Real3rr0r303YT/bruce-d0wd.git
   cd bruce-d0wd
   pio run
   ```
3. Flash directly over USB:
   ```bash
   pio run -t upload
   pio device monitor      # 115200 baud
   ```

## Flashing from the browser

The GitHub Pages site exposes an **Install** button powered by ESP Web Tools.
It uses the Web Serial API, so use **Chrome** or **Edge** on desktop.

To make browser flashing work you must publish a merged binary at
`firmware/bruce-d0wd-merged.bin`. Generate it after a build:

```bash
esptool.py --chip esp32 merge_bin -o firmware/bruce-d0wd-merged.bin \
  --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x1000  .pio/build/esp32-d0wd/bootloader.bin \
  0x8000  .pio/build/esp32-d0wd/partitions.bin \
  0xe000  ~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/esp32-d0wd/firmware.bin
```

Then enable **GitHub Pages** for the repository (Settings → Pages → deploy from
the default branch) and open the published URL.

## Extending the firmware

`src/modules.cpp` is the place to add functionality. Each `probe*()` function
already brings its peripheral up over the shared bus, so you have a working
handle (`nrf`, `ELECHOUSE_cc1101`, `rc522`, `WiFi`, the IR pins) to build on.
Keep any additions lawful and limited to authorized use.

## Dependencies

Pulled in automatically by PlatformIO (see `platformio.ini`):

- Adafruit SSD1306 + Adafruit GFX + Adafruit BusIO
- RF24 (NRF24L01)
- SmartRC-CC1101-Driver-Lib (CC1101)
- IRremoteESP8266 (infrared)
- MFRC522 (RC522 RFID)

## Support

If this project helps you, you can support it here:
**https://buymeacoffee.com/3rr0r303YT**

## License

Released under the **GNU General Public License v3.0**. See [LICENSE](LICENSE).
