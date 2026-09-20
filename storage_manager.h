#pragma once

#include <stdint.h>
#include "alarm_manager.h"

class StorageManager {
public:
    bool begin();

    // Saves every user setting that must survive power cycling. RTC time/date
    // remain in the DS3231 backup domain rather than in NVS.
    bool saveAll(const Alarm alarms[NUM_ALARMS],
                 bool use12Hour,
                 int8_t dstOffsetHours,
                 bool autoBrightness,
                 uint8_t manualBrightnessLevel);

    bool loadAll(Alarm alarms[NUM_ALARMS],
                 bool &use12Hour,
                 int8_t &dstOffsetHours,
                 bool &autoBrightness,
                 uint8_t &manualBrightnessLevel);

private:
    struct PersistedAlarm {
        uint8_t enabled;
        uint8_t type;
        uint8_t hour;
        uint8_t minute;
        uint8_t date;
        uint8_t month;
        uint8_t year;
        uint8_t toneIndex;
        uint8_t snoozeDelayMin;
        uint8_t maxSnoozeCount;
        uint16_t autoSilenceMin;
    };

    struct PersistedState {
        uint32_t magic;
        uint8_t version;
        uint8_t use12Hour;
        int8_t dstOffsetHours;
        uint8_t autoBrightness;
        uint8_t manualBrightnessLevel;
        PersistedAlarm alarm[NUM_ALARMS];
    };
};
