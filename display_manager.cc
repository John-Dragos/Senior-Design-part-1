#include "display_manager.h"

#include <stdio.h>
#include <string.h>

#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace {
static const char* TAG = "DisplayManager";
static const char* DAYS_OF_WEEK[] = {
    "", "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
};

// ASCII 32 (' ') through 58 (':'), then A-Z, [, ].
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // 32 space
    {0x00,0x00,0x5F,0x00,0x00}, // !
    {0x00,0x07,0x00,0x07,0x00}, // "
    {0x14,0x7F,0x14,0x7F,0x14}, // #
    {0x24,0x2A,0x7F,0x2A,0x12}, // $
    {0x23,0x13,0x08,0x64,0x62}, // %
    {0x36,0x49,0x55,0x22,0x50}, // &
    {0x00,0x05,0x03,0x00,0x00}, // '
    {0x00,0x1C,0x22,0x41,0x00}, // (
    {0x00,0x41,0x22,0x1C,0x00}, // )
    {0x14,0x08,0x3E,0x08,0x14}, // *
    {0x08,0x08,0x3E,0x08,0x08}, // +
    {0x00,0x50,0x30,0x00,0x00}, // ,
    {0x08,0x08,0x08,0x08,0x08}, // -
    {0x00,0x60,0x60,0x00,0x00}, // .
    {0x20,0x10,0x08,0x04,0x02}, // /
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9
    {0x00,0x36,0x36,0x00,0x00}, // :
    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x09,0x19,0x66}, // R
    {0x26,0x49,0x49,0x49,0x32}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x3F,0x40,0x38,0x40,0x3F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
    {0x3F,0x20,0x20,0x20,0x3F}, // [
    {0x3F,0x04,0x04,0x04,0x3F}  // ]
};

static void rotateBuffer180(uint8_t* buffer) {
    uint8_t temp[OLED_FRAMEBUFFER_BYTES] = {};
    for (int page = 0; page < 8; ++page) {
        for (int x = 0; x < 128; ++x) {
            const uint8_t byteVal = buffer[x + page * 128];
            for (int bit = 0; bit < 8; ++bit) {
                if ((byteVal & (1U << bit)) == 0) continue;
                const int srcX = x;
                const int srcY = page * 8 + bit;
                const int dstX = 127 - srcX;
                const int dstY = 63 - srcY;
                const int dstPage = dstY / 8;
                const int dstBit = dstY % 8;
                temp[dstX + dstPage * 128] |= static_cast<uint8_t>(1U << dstBit);
            }
        }
    }
    memcpy(buffer, temp, sizeof(temp));
}
}

void DisplayManager::begin() {
    const uint8_t initCmds[] = {
        0x00,
        0xAE,
        0xD5, 0x80,
        0xA8, 0x3F,
        0xD3, 0x00,
        0x40,
        0x8D, 0x14,
        0x20, 0x00,
        0xA1,
        0xC8,
        0xDA, 0x12,
        0x81, 0xCF,
        0xD9, 0xF1,
        0xDB, 0x30,
        0xA4,
        0xA6,
        0xAF
    };

    esp_err_t err = i2c_master_write_to_device(
        I2C_MASTER_NUM, SSD1306_ADDR,
        initCmds, sizeof(initCmds),
        pdMS_TO_TICKS(50));

    if (err == ESP_OK) {
        initialized = true;
        memset(oledBuffer, 0, sizeof(oledBuffer));
        setBrightness(180);
        ESP_LOGI(TAG, "SSD1306 initialized");
    } else {
        initialized = false;
        ESP_LOGE(TAG, "SSD1306 initialization failed: %d", err);
    }
}

void DisplayManager::drawText(int x, int y, const char* text, int scale) {
    if (!text || scale < 1) return;

    int cursorX = x;
    while (*text != '\0') {
        const char c = *text++;
        int fontIndex = 0;

        if (c >= ' ' && c <= ':') {
            fontIndex = c - ' ';
        } else if (c >= 'A' && c <= 'Z') {
            fontIndex = 27 + (c - 'A');
        } else if (c == '[') {
            fontIndex = 53;
        } else if (c == ']') {
            fontIndex = 54;
        } else {
            fontIndex = 0;
        }

        for (int i = 0; i < 5; ++i) {
            const uint8_t column = font5x7[fontIndex][i];
            for (int bit = 0; bit < 8; ++bit) {
                if ((column & (1U << bit)) == 0) continue;
                for (int sx = 0; sx < scale; ++sx) {
                    for (int sy = 0; sy < scale; ++sy) {
                        const int targetX = cursorX + i * scale + sx;
                        const int targetY = y + bit * scale + sy;
                        if (targetX < 0 || targetX >= 128 || targetY < 0 || targetY >= 64) {
                            continue;
                        }
                        const int page = targetY / 8;
                        const int bitPos = targetY % 8;
                        oledBuffer[targetX + page * 128] |= static_cast<uint8_t>(1U << bitPos);
                    }
                }
            }
        }

        cursorX += (5 + 1) * scale;
        if (cursorX >= 128) break;
    }
}

void DisplayManager::updateScreen() {
    if (!initialized) return;

    const uint8_t colCmds[] = {0x00, 0x21, 0x00, 0x7F};
    i2c_master_write_to_device(I2C_MASTER_NUM, SSD1306_ADDR,
                               colCmds, sizeof(colCmds), pdMS_TO_TICKS(50));

    const uint8_t pageCmds[] = {0x00, 0x22, 0x00, 0x07};
    i2c_master_write_to_device(I2C_MASTER_NUM, SSD1306_ADDR,
                               pageCmds, sizeof(pageCmds), pdMS_TO_TICKS(50));

    uint8_t payload[OLED_FRAMEBUFFER_BYTES + 1];
    payload[0] = 0x40;
    memcpy(&payload[1], oledBuffer, OLED_FRAMEBUFFER_BYTES);
    i2c_master_write_to_device(I2C_MASTER_NUM, SSD1306_ADDR,
                               payload, sizeof(payload), pdMS_TO_TICKS(50));
}

void DisplayManager::renderClockFace(uint8_t h, uint8_t m, uint8_t s,
                                     uint8_t day, uint8_t date, uint8_t month,
                                     uint8_t year, bool use12Hour,
                                     bool anyAlarmEnabled, int8_t dstOffsetHours) {
    memset(oledBuffer, 0, sizeof(oledBuffer));

    char timeStr[12] = {};
    char amPm[3] = {};
    char dateStr[24] = {};
    char status[4] = {};

    uint8_t displayHour = h;
    if (use12Hour) {
        snprintf(amPm, sizeof(amPm), "%s", h >= 12 ? "PM" : "AM");
        if (h == 0) displayHour = 12;
        else if (h > 12) displayHour = static_cast<uint8_t>(h - 12);
    }
    snprintf(timeStr, sizeof(timeStr), "%02u:%02u:%02u", displayHour, m, s);
    snprintf(dateStr, sizeof(dateStr), "%s %02u/%02u/20%02u",
             (day >= 1 && day <= 7) ? DAYS_OF_WEEK[day] : "ERR",
             month, date, year);

    if (dstOffsetHours != 0) {
        strcpy(status, "DST");
        drawText(72, 0, status, 1);
    }
    if (anyAlarmEnabled) {
        drawText(106, 0, "A", 1);
    }

    drawText(0, 16, timeStr, 2);
    if (use12Hour) {
        drawText(100, 26, amPm, 1);
    }
    drawText(0, 52, dateStr, 1);

    rotateBuffer180(oledBuffer);
    updateScreen();
}

void DisplayManager::renderLines(const char* line1, const char* line2,
                                 const char* line3, const char* line4) {
    memset(oledBuffer, 0, sizeof(oledBuffer));
    if (line1) drawText(0, 0, line1, 1);
    if (line2) drawText(0, 16, line2, 1);
    if (line3) drawText(0, 32, line3, 1);
    if (line4) drawText(0, 48, line4, 1);
    rotateBuffer180(oledBuffer);
    updateScreen();
}

void DisplayManager::renderTitleValue(const char* title, const char* value,
                                      const char* hint) {
    renderLines(title, value, hint, nullptr);
}

void DisplayManager::renderMenu(const char* item, uint8_t index, uint8_t count) {
    char position[16];
    snprintf(position, sizeof(position), "%u/%u", index + 1, count);

    memset(oledBuffer, 0, sizeof(oledBuffer));
    drawText(0, 0, "MENU", 1);
    drawText(106, 0, position, 1);
    drawText(0, 22, "[", 1);
    drawText(7, 22, item, 2);
    drawText(0, 48, "ROTATE OR CLICK,", 1);

    rotateBuffer180(oledBuffer);
    updateScreen();
}

void DisplayManager::setBrightness(uint8_t level) {
    if (!initialized) return;
    if (lastBrightness == level) return;

    const uint8_t contrastCmds[] = {0x00, 0x81, level};
    i2c_master_write_to_device(I2C_MASTER_NUM, SSD1306_ADDR,
                               contrastCmds, sizeof(contrastCmds),
                               pdMS_TO_TICKS(50));
    lastBrightness = level;
}

void DisplayManager::setInverted(bool invert) {
    if (!initialized || invert == isInverted) return;

    const uint8_t cmd[] = {0x00, static_cast<uint8_t>(invert ? 0xA7 : 0xA6)};
    i2c_master_write_to_device(I2C_MASTER_NUM, SSD1306_ADDR,
                               cmd, sizeof(cmd), pdMS_TO_TICKS(50));
    isInverted = invert;
}

void DisplayManager::processAlarmFlashing(bool isRinging) {
    if (!isRinging) {
        if (isInverted) {
            setInverted(false);
        }
        return;
    }

    const uint32_t currentMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    if (currentMs - lastFlashTime >= FLASH_INTERVAL_MS) {
        setInverted(!isInverted);
        lastFlashTime = currentMs;
    }
}
