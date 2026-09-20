#include "time_manager.h"
#include "config.h"

extern "C" {
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
}

namespace {
static const char* TAG = "TimeManager";

static bool isLeapYear(uint16_t fullYear) {
    return (fullYear % 4 == 0 && fullYear % 100 != 0) || (fullYear % 400 == 0);
}
}

uint8_t TimeManager::bcdToDec(uint8_t val) {
    return static_cast<uint8_t>((val >> 4) * 10 + (val & 0x0F));
}

uint8_t TimeManager::decToBcd(uint8_t val) {
    return static_cast<uint8_t>(((val / 10) << 4) | (val % 10));
}

uint8_t TimeManager::daysInMonth(uint8_t year, uint8_t month) {
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 31;

    if (month == 2) {
        return isLeapYear(static_cast<uint16_t>(2000 + year)) ? 29 : 28;
    }
    return days[month - 1];
}

uint8_t TimeManager::dayOfWeek(uint8_t year, uint8_t month, uint8_t date) {
    // Sakamoto algorithm, returns 0=Sunday..6=Saturday.
    static const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    uint16_t y = static_cast<uint16_t>(2000 + year);
    if (month < 3) --y;
    uint8_t dow0 = static_cast<uint8_t>(
        (y + y / 4 - y / 100 + y / 400 + t[month - 1] + date) % 7);
    return static_cast<uint8_t>(dow0 + 1); // 1=Sun..7=Sat
}

void TimeManager::begin() {
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = PIN_I2C_SDA;
    conf.scl_io_num = PIN_I2C_SCL;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    conf.clk_flags = 0;

    // The original project initialized the legacy I2C driver here. Keep that
    // architecture so the OLED and DS3231 share one bus.
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &conf));
    esp_err_t installErr = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (installErr != ESP_OK && installErr != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(installErr);
    }

    rtcReady = readRtc();
    if (!rtcReady) {
        ESP_LOGE(TAG, "DS3231 read failed; clock display will use the last valid values");
    }

    lastReadTimeMs = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
}

void TimeManager::update() {
    const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
    if (nowMs - lastReadTimeMs >= 250ULL) {
        // Polling faster than once per second gives the application enough
        // opportunity to catch each second boundary without busy waiting.
        readRtc();
        lastReadTimeMs = nowMs;
    }
}

bool TimeManager::readRtc() {
    uint8_t regAddr = 0x00;
    uint8_t data[7] = {};

    esp_err_t err = i2c_master_write_read_device(
        I2C_MASTER_NUM, DS3231_ADDR,
        &regAddr, 1,
        data, sizeof(data),
        pdMS_TO_TICKS(50));

    if (err != ESP_OK) {
        return false;
    }

    currentSecond = bcdToDec(data[0] & 0x7F);
    currentMinute = bcdToDec(data[1] & 0x7F);

    const uint8_t hourReg = data[2];
    if (hourReg & 0x40) {
        // DS3231 12-hour mode: bit 5 = PM, bits 4..0 = hour.
        uint8_t h12 = bcdToDec(hourReg & 0x1F);
        bool pm = (hourReg & 0x20) != 0;
        if (h12 == 12) {
            currentHour = pm ? 12 : 0;
        } else {
            currentHour = static_cast<uint8_t>(h12 + (pm ? 12 : 0));
        }
    } else {
        currentHour = bcdToDec(hourReg & 0x3F);
    }

    currentDay = static_cast<uint8_t>(bcdToDec(data[3] & 0x07));
    currentDate = bcdToDec(data[4] & 0x3F);
    currentMonth = bcdToDec(data[5] & 0x1F);
    currentYear = bcdToDec(data[6]);

    // Sanity check protects the UI/alarm code from corrupt bus reads.
    if (currentHour > 23 || currentMinute > 59 || currentSecond > 59 ||
        currentDate < 1 || currentDate > 31 || currentMonth < 1 || currentMonth > 12 ||
        currentDay < 1 || currentDay > 7) {
        return false;
    }

    rtcReady = true;
    return true;
}

bool TimeManager::writeRtc(uint8_t hour, uint8_t minute, uint8_t second,
                           uint8_t date, uint8_t month, uint8_t year,
                           uint8_t day) {
    uint8_t data[] = {
        0x00,
        decToBcd(second),
        decToBcd(minute),
        decToBcd(hour), // force 24-hour mode; bit 6 = 0
        decToBcd(day),
        decToBcd(date),
        decToBcd(month),
        decToBcd(year)
    };

    esp_err_t err = i2c_master_write_to_device(
        I2C_MASTER_NUM, DS3231_ADDR,
        data, sizeof(data),
        pdMS_TO_TICKS(50));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DS3231 write failed: %d", err);
        return false;
    }

    // Clear the oscillator-stop flag after a valid manual set.
    uint8_t statusWrite[] = {0x0F, 0x00};
    i2c_master_write_to_device(I2C_MASTER_NUM, DS3231_ADDR,
                               statusWrite, sizeof(statusWrite),
                               pdMS_TO_TICKS(50));

    currentHour = hour;
    currentMinute = minute;
    currentSecond = second;
    currentDay = day;
    currentDate = date;
    currentMonth = month;
    currentYear = year;
    rtcReady = true;
    return true;
}

bool TimeManager::setDateTime(uint8_t hour, uint8_t minute, uint8_t second,
                              uint8_t date, uint8_t month, uint8_t year) {
    if (hour > 23 || minute > 59 || second > 59 || month < 1 || month > 12 ||
        date < 1 || date > daysInMonth(year, month)) {
        return false;
    }

    const uint8_t day = dayOfWeek(year, month, date);
    return writeRtc(hour, minute, second, date, month, year, day);
}

bool TimeManager::adjustHours(int8_t deltaHours) {
    if (deltaHours == 0) return true;

    int totalMinutes = static_cast<int>(currentHour) * 60 + currentMinute;
    totalMinutes += static_cast<int>(deltaHours) * 60;

    int dayOffset = 0;
    while (totalMinutes < 0) {
        totalMinutes += 24 * 60;
        --dayOffset;
    }
    while (totalMinutes >= 24 * 60) {
        totalMinutes -= 24 * 60;
        ++dayOffset;
    }

    // Convert the current calendar date by +/- day as needed.
    int y = 2000 + currentYear;
    int mo = currentMonth;
    int d = currentDate + dayOffset;

    while (d < 1) {
        if (--mo < 1) { mo = 12; --y; }
        const uint8_t yy = static_cast<uint8_t>((y >= 2000 && y <= 2099) ? y - 2000 : 0);
        d += daysInMonth(yy, static_cast<uint8_t>(mo));
    }
    while (true) {
        const uint8_t yy = static_cast<uint8_t>((y >= 2000 && y <= 2099) ? y - 2000 : 0);
        const uint8_t dim = daysInMonth(yy, static_cast<uint8_t>(mo));
        if (d <= dim) break;
        d -= dim;
        if (++mo > 12) { mo = 1; ++y; }
    }

    if (y < 2000 || y > 2099) {
        return false;
    }

    const uint8_t newYear = static_cast<uint8_t>(y - 2000);
    const uint8_t newMonth = static_cast<uint8_t>(mo);
    const uint8_t newDate = static_cast<uint8_t>(d);
    const uint8_t newHour = static_cast<uint8_t>(totalMinutes / 60);
    const uint8_t newMinute = static_cast<uint8_t>(totalMinutes % 60);

    return writeRtc(newHour, newMinute, currentSecond,
                    newDate, newMonth, newYear,
                    dayOfWeek(newYear, newMonth, newDate));
}

void TimeManager::getCurrentTime(uint8_t &h, uint8_t &m, uint8_t &s,
                                 uint8_t &day, uint8_t &date, uint8_t &month,
                                 uint8_t &year) {
    h = currentHour;
    m = currentMinute;
    s = currentSecond;
    day = currentDay;
    date = currentDate;
    month = currentMonth;
    year = currentYear;
}
