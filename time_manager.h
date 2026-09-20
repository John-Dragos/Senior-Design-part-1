#pragma once

#include <stdint.h>

class TimeManager {
public:
    void begin();
    void update();

    void getCurrentTime(uint8_t &h, uint8_t &m, uint8_t &s,
                        uint8_t &day, uint8_t &date, uint8_t &month,
                        uint8_t &year);

    bool setDateTime(uint8_t hour, uint8_t minute, uint8_t second,
                     uint8_t date, uint8_t month, uint8_t year);
    bool adjustHours(int8_t deltaHours);

    static uint8_t dayOfWeek(uint8_t year, uint8_t month, uint8_t date); // 1=Sun..7=Sat
    static uint8_t daysInMonth(uint8_t year, uint8_t month);

private:
    uint8_t currentHour = 0;
    uint8_t currentMinute = 0;
    uint8_t currentSecond = 0;
    uint8_t currentDay = 1;
    uint8_t currentDate = 1;
    uint8_t currentMonth = 1;
    uint8_t currentYear = 26;

    uint64_t lastReadTimeMs = 0;
    bool rtcReady = false;

    static uint8_t bcdToDec(uint8_t val);
    static uint8_t decToBcd(uint8_t val);
    bool readRtc();
    bool writeRtc(uint8_t hour, uint8_t minute, uint8_t second,
                  uint8_t date, uint8_t month, uint8_t year,
                  uint8_t day);
};
