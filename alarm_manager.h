#pragma once

#include <stdint.h>
#include "config.h"

enum AlarmType : uint8_t {
    ALARM_DAILY = 0,
    ALARM_DATE = 1
};

enum AlarmState : uint8_t {
    ALARM_OFF = 0,
    ALARM_READY = 1,
    ALARM_RINGING = 2,
    ALARM_SNOOZED = 3
};

struct Alarm {
    AlarmType type = ALARM_DAILY;
    AlarmState state = ALARM_OFF;

    uint8_t hour = ALARM_DEFAULT_HOUR;
    uint8_t minute = ALARM_DEFAULT_MINUTE;
    uint8_t date = 1;
    uint8_t month = 1;
    uint8_t year = 26; // 20YY

    uint8_t toneIndex = 0;      // 0..2
    uint8_t snoozeDelayMin = 9; // 5..15
    uint8_t maxSnoozeCount = 3; // 1..10, 0 = unlimited
    uint8_t currentSnoozeCount = 0;
    uint16_t autoSilenceMin = 15; // 15/30/60, 0 = unlimited

    // Runtime-only timers. Never persisted.
    uint64_t snoozeStartTimeMs = 0;
    uint64_t ringStartTimeMs = 0;
};

class AlarmManager {
public:
    void begin();

    void update(uint8_t h, uint8_t m, uint8_t s,
                uint8_t date, uint8_t month, uint8_t year);

    Alarm& getAlarm(uint8_t index);

    void setAlarmTime(uint8_t index, uint8_t hour, uint8_t minute);
    void setAlarmDate(uint8_t index, uint8_t date, uint8_t month, uint8_t year);
    void setAlarmConfig(uint8_t index, const Alarm& config);
    void toggleAlarm(uint8_t index, bool active);

    void snoozeActive();
    void stopActive();

    bool isRinging() const;
    int getActiveRingingIndex() const;
    bool consumePersistenceRequest();

private:
    Alarm alarms[NUM_ALARMS];
    int activeRingingIndex = -1;

    uint64_t lastBeepTimeMs = 0;
    bool isBeeping = false;
    uint8_t lastToneIndex = 255;
    bool persistenceRequest = false;

    void setBuzzerFrequency(uint32_t freqHz, bool on);
    void updateBuzzerTone(uint64_t currentMillis, uint8_t toneIndex);
    void promotePendingRingingAlarm();
    void finishAlarm(Alarm& alarm);
    static bool dateMatches(const Alarm& alarm,
                            uint8_t date, uint8_t month, uint8_t year);
};
