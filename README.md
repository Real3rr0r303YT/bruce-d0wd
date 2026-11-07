# ESP Multitool (ESP32-D0WD + SSD1306)

Connected multi-tool firmware for the **ESP32-D0WD** with a **0.96" SSD1306 OLED**.
Easy Wi-Fi onboarding via a QR code on the display, a built-in web remote
(with file upload and OTA updates), plus IR, BLE scan and a 2.4 GHz NRF24
channel scanner — all driven from a three-button OLED menu.

> **Scope:** receive / scan / own-device control only — **no jamming, deauth or
> spam**. For education and authorized testing; you are responsible for lawful use.

## Flash it from your browser

Open the GitHub Pages site and click **Install ESP Multitool** (Chrome/Edge on
desktop). Connect the board over USB; if it isn't detected, hold `BOOT`, tap
`RST`, release `BOOT`, then click Install.

The installer flashes the complete image `firmware/esp-multitool-full.bin` at
offset `0x0`.

## Features

- **Wi-Fi onboarding by QR** — the OLED shows a QR code; scan it with your phone
  to join the `ESP-MT` setup network and enter your home Wi-Fi.
- **Web remote** — once online, a built-in web UI controls the device, uploads
  files (IR code sets, images, config) and performs **OTA** firmware updates.
- **IR** — record and replay remotes, send stored IR codes.
- **Bluetooth LE** — scan nearby devices (name / MAC / RSSI).
- **NRF24 scanner** — 2.4 GHz channel / spectrum monitor and passive sniffer
  (receive only).
- **Local control** — three-button OLED menu (Up / Down / Select; Up+Down = back).

## Hardware pinout

| Peripheral   | Signal              | ESP32 GPIO        |
|--------------|---------------------|-------------------|
| OLED (I2C)   | SDA / SCL           | 21 / 22 (0x3C)    |
| Buttons      | Select / Up / Down  | 14 / 32 / 33      |
| NRF24L01     | CE / CSN            | 4 / 5             |
| SPI bus      | SCK / MISO / MOSI   | 18 / 19 / 23      |
| Infrared     | TX / RX             | 12 / 13           |

> The display is a "yellow-blue" SSD1306: the top 16 px are physically yellow,
> the rest blue — a hardware property of the panel, not a software setting. The
> setup QR is drawn fully inside the blue area so it scans cleanly.

## Firmware files

- `firmware/esp-multitool-full.bin` — complete image (bootloader + partitions +
  app), flash at `0x0` (used by the browser installer).
- `firmware/esp-multitool-app.bin` — application only, for **OTA** updates
  through the web UI.

## Manual flashing (USB)

Put the board in download mode (hold `BOOT`, tap `RST`, release `BOOT`; unplug
the NRF24 to avoid power dips), then:

```bash
esptool --chip esp32 --port <PORT> --baud 460800 write-flash 0x0 firmware/esp-multitool-full.bin
```

If the transfer drops out ("invalid packet"), lower the baud to `115200`.

## After flashing

1. Press `RST`. The OLED shows **Wi-Fi Setup** with a QR code.
2. Scan it with your phone to join `ESP-MT`, then enter your home Wi-Fi.
3. The device reboots and shows a QR with its IP — scan it to open the web remote.
4. From then on you can update over the air from the web UI, no cable needed.

## Support

If this project helps you: **https://buymeacoffee.com/3rr0r303YT**

## Also in this repo

`scaffold.html` + `firmware/bruce-d0wd-merged.bin` are the earlier hardware
bring-up scaffold (OLED + module detection). The `src/` and `platformio.ini`
here build that scaffold; the ESP Multitool itself is built from its own
PlatformIO project.

## License

GNU General Public License v3.0 — see [LICENSE](LICENSE).
