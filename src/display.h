// ============================================================================
//  display.h  -  thin wrapper around the SSD1306 OLED
// ============================================================================
#pragma once
#include <Arduino.h>

namespace Display {
    // Initialize the I2C bus and SSD1306 panel. Returns false if the panel
    // did not acknowledge on the I2C bus.
    bool begin();

    // Draw the splash / boot banner.
    void splash();

    // Render a titled list menu. `items` holds `count` labels, `selected` is
    // the highlighted index.
    void drawMenu(const char* title, const char* const* items, int count, int selected);

    // Render a full-screen status page with up to `count` text lines.
    void drawStatus(const char* title, const char* const* lines, int count);

    // Simple two-line toast, centered.
    void drawMessage(const char* line1, const char* line2);
}
