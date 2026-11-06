// ============================================================================
//  display.cpp  -  SSD1306 OLED rendering helpers
// ============================================================================
#include "display.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

static Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

namespace Display {

bool begin() {
    Wire.begin(OLED_SDA, OLED_SCL);
    if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
        return false;
    }
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.display();
    return true;
}

void splash() {
    oled.clearDisplay();
    oled.setTextSize(2);
    oled.setCursor(0, 8);
    oled.println(FW_NAME);
    oled.setTextSize(1);
    oled.setCursor(0, 40);
    oled.print("ESP32-D0WD  v");
    oled.println(FW_VERSION);
    oled.setCursor(0, 52);
    oled.println("hardware bring-up");
    oled.display();
}

void drawMenu(const char* title, const char* const* items, int count, int selected) {
    oled.clearDisplay();

    // Title bar
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    oled.fillRect(0, 0, OLED_WIDTH, 11, SSD1306_WHITE);
    oled.setCursor(2, 2);
    oled.println(title);
    oled.setTextColor(SSD1306_WHITE);

    // Visible window of 5 rows
    const int rows = 5;
    int first = 0;
    if (selected >= rows) first = selected - rows + 1;

    for (int i = 0; i < rows && (first + i) < count; i++) {
        int idx = first + i;
        int y = 14 + i * 10;
        if (idx == selected) {
            oled.fillRect(0, y - 1, OLED_WIDTH, 10, SSD1306_WHITE);
            oled.setTextColor(SSD1306_BLACK);
        } else {
            oled.setTextColor(SSD1306_WHITE);
        }
        oled.setCursor(3, y);
        oled.println(items[idx]);
    }
    oled.setTextColor(SSD1306_WHITE);
    oled.display();
}

void drawStatus(const char* title, const char* const* lines, int count) {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    oled.fillRect(0, 0, OLED_WIDTH, 11, SSD1306_WHITE);
    oled.setCursor(2, 2);
    oled.println(title);
    oled.setTextColor(SSD1306_WHITE);

    for (int i = 0; i < count; i++) {
        oled.setCursor(2, 14 + i * 10);
        oled.println(lines[i]);
    }
    oled.display();
}

void drawMessage(const char* line1, const char* line2) {
    oled.clearDisplay();
    oled.setTextSize(1);
    oled.setTextColor(SSD1306_WHITE);
    oled.setCursor(0, 24);
    oled.println(line1);
    if (line2) {
        oled.setCursor(0, 36);
        oled.println(line2);
    }
    oled.display();
}

} // namespace Display
