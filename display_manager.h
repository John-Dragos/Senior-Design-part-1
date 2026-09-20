#pragma once

#include <stdint.h>
#include "config.h"

class DisplayManager {
public:
    void begin();

    void renderClockFace(uint8_t h, uint8_t m, uint8_t s,
                         uint8_t day, uint8_t date, uint8_t month, uint8_t year,
                         bool use12Hour, bool anyAlarmEnabled,
                         int8_t dstOffsetHours);

    void renderTitleValue(const char* title, const char* value,
                          const char* hint = nullptr);
    void renderMenu(const char* item, uint8_t index, uint8_t count);

    void setBrightness(uint8_t level);
    void setInverted(bool invert);
    void processAlarmFlashing(bool isRinging);

private:
    uint8_t oledBuffer[OLED_FRAMEBUFFER_BYTES] = {};
    uint32_t lastFlashTime = 0;
    bool isInverted = false;
    uint8_t lastBrightness = 255;
    bool initialized = false;

    void drawText(int x, int y, const char* text, int scale = 1);
    void updateScreen();
    void renderLines(const char* line1, const char* line2,
                     const char* line3, const char* line4);
};
