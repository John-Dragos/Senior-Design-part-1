#include "alarm_manager.h"

#include "driver/ledc.h"
#include "esp_timer.h"

namespace {
constexpr ledc_mode_t BUZZER_SPEED_MODE = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t BUZZER_TIMER = LEDC_TIMER_0;
constexpr ledc_channel_t BUZZER_CHANNEL = LEDC_CHANNEL_0;
}

void AlarmManager::begin() {
    ledc_timer_config_t ledcTimer = {};
    ledcTimer.speed_mode = BUZZER_SPEED_MODE;
    ledcTimer.duty_resolution = LEDC_TIMER_13_BIT;
    ledcTimer.timer_num = BUZZER_TIMER;
    ledcTimer.freq_hz = 2500;
    ledcTimer.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&ledcTimer);

    ledc_channel_config_t ledcChannel = {};
    ledcChannel.speed_mode = BUZZER_SPEED_MODE;
    ledcChannel.channel = BUZZER_CHANNEL;
    ledcChannel.timer_sel = BUZZER_TIMER;
    ledcChannel.gpio_num = PIN_BUZZER;
    ledcChannel.duty = 0;
    ledcChannel.hpoint = 0;
    ledc_channel_config(&ledcChannel);

    // Defaults satisfy the required mix: daily + dated + daily.
    alarms[0].type = ALARM_DAILY;
    alarms[0].state = ALARM_READY;
    alarms[0].hour = 7;
    alarms[0].minute = 0;

    alarms[1].type = ALARM_DATE;
    alarms[1].state = ALARM_OFF;
    alarms[1].hour = 8;
    alarms[1].minute = 0;

    alarms[2].type = ALARM_DAILY;
    alarms[2].state = ALARM_OFF;
    alarms[2].hour = 9;
    alarms[2].minute = 0;
}

bool AlarmManager::dateMatches(const Alarm& alarm,
                               uint8_t date, uint8_t month, uint8_t year) {
    if (alarm.type == ALARM_DAILY) {
        return true;
    }
    return alarm.date == date && alarm.month == month && alarm.year == year;
}

void AlarmManager::update(uint8_t h, uint8_t m, uint8_t s,
                          uint8_t date, uint8_t month, uint8_t year) {
    const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;

    // 1) Trigger matching alarms once when the RTC reaches HH:MM:00.
    if (s == 0) {
        for (int i = 0; i < NUM_ALARMS; ++i) {
            Alarm& alarm = alarms[i];
            if (alarm.state != ALARM_READY) {
                continue;
            }

            if (alarm.hour == h && alarm.minute == m && dateMatches(alarm, date, month, year)) {
                alarm.state = ALARM_RINGING;
                alarm.ringStartTimeMs = nowMs;
                alarm.currentSnoozeCount = 0;
            }
        }
    }

    // 2) Snooze expiry.
    for (int i = 0; i < NUM_ALARMS; ++i) {
        Alarm& alarm = alarms[i];
        if (alarm.state != ALARM_SNOOZED) {
            continue;
        }

        const uint64_t snoozeDurationMs =
            static_cast<uint64_t>(alarm.snoozeDelayMin) * 60ULL * 1000ULL;
        if (nowMs - alarm.snoozeStartTimeMs >= snoozeDurationMs) {
            alarm.state = ALARM_RINGING;
            alarm.ringStartTimeMs = nowMs;
        }
    }

    // 3) Automatically silence ringing alarms after the configured time.
    for (int i = 0; i < NUM_ALARMS; ++i) {
        Alarm& alarm = alarms[i];
        if (alarm.state != ALARM_RINGING || alarm.autoSilenceMin == 0) {
            continue;
        }

        const uint64_t silenceDurationMs =
            static_cast<uint64_t>(alarm.autoSilenceMin) * 60ULL * 1000ULL;
        if (nowMs - alarm.ringStartTimeMs >= silenceDurationMs) {
            finishAlarm(alarm);
        }
    }

    // If the active alarm was silenced or timed out, promote another one that may
    // have triggered at the same time. This allows multiple alarms to coexist.
    if (activeRingingIndex >= 0 && alarms[activeRingingIndex].state != ALARM_RINGING) {
        activeRingingIndex = -1;
    }
    promotePendingRingingAlarm();

    // 4) Drive the currently active tone non-blockingly.
    if (activeRingingIndex >= 0) {
        Alarm& active = alarms[activeRingingIndex];
        if (active.toneIndex != lastToneIndex) {
            isBeeping = false;
            lastBeepTimeMs = nowMs;
            lastToneIndex = active.toneIndex;
        }
        updateBuzzerTone(nowMs, active.toneIndex);
    } else {
        setBuzzerFrequency(2500, false);
        isBeeping = false;
        lastToneIndex = 255;
    }
}

void AlarmManager::promotePendingRingingAlarm() {
    if (activeRingingIndex >= 0) {
        return;
    }

    for (int i = 0; i < NUM_ALARMS; ++i) {
        if (alarms[i].state == ALARM_RINGING) {
            activeRingingIndex = i;
            lastToneIndex = 255;
            return;
        }
    }
}

void AlarmManager::finishAlarm(Alarm& alarm) {
    const AlarmState previousState = alarm.state;
    alarm.currentSnoozeCount = 0;
    alarm.state = (alarm.type == ALARM_DAILY) ? ALARM_READY : ALARM_OFF;
    if (previousState != alarm.state && alarm.type == ALARM_DATE) {
        persistenceRequest = true;
    }
    alarm.ringStartTimeMs = 0;
    alarm.snoozeStartTimeMs = 0;
}

void AlarmManager::snoozeActive() {
    if (activeRingingIndex < 0) {
        return;
    }

    Alarm& alarm = alarms[activeRingingIndex];
    if (alarm.state != ALARM_RINGING) {
        activeRingingIndex = -1;
        return;
    }

    if (alarm.maxSnoozeCount == 0 || alarm.currentSnoozeCount < alarm.maxSnoozeCount) {
        ++alarm.currentSnoozeCount;
        alarm.state = ALARM_SNOOZED;
        alarm.snoozeStartTimeMs = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
    } else {
        finishAlarm(alarm);
    }

    setBuzzerFrequency(2500, false);
    activeRingingIndex = -1;
    promotePendingRingingAlarm();
}

void AlarmManager::stopActive() {
    if (activeRingingIndex < 0) {
        return;
    }

    finishAlarm(alarms[activeRingingIndex]);
    setBuzzerFrequency(2500, false);
    activeRingingIndex = -1;
    promotePendingRingingAlarm();
}

void AlarmManager::setBuzzerFrequency(uint32_t freqHz, bool on) {
    if (on) {
        ledc_set_freq(BUZZER_SPEED_MODE, BUZZER_TIMER, freqHz);
        ledc_set_duty(BUZZER_SPEED_MODE, BUZZER_CHANNEL, BUZZER_DUTY);
    } else {
        ledc_set_duty(BUZZER_SPEED_MODE, BUZZER_CHANNEL, 0);
    }
    ledc_update_duty(BUZZER_SPEED_MODE, BUZZER_CHANNEL);
}

void AlarmManager::updateBuzzerTone(uint64_t currentMillis, uint8_t toneIndex) {
    toneIndex %= 3;

    uint32_t intervalMs;
    uint32_t frequencyHz;
    switch (toneIndex) {
        case 0:
            intervalMs = 500;
            frequencyHz = 2200;
            break;
        case 1:
            intervalMs = 180;
            frequencyHz = 2800;
            break;
        default:
            intervalMs = 90;
            frequencyHz = 3400;
            break;
    }

    if (currentMillis - lastBeepTimeMs >= intervalMs) {
        isBeeping = !isBeeping;
        setBuzzerFrequency(frequencyHz, isBeeping);
        lastBeepTimeMs = currentMillis;
    }
}

Alarm& AlarmManager::getAlarm(uint8_t index) {
    return alarms[index % NUM_ALARMS];
}

void AlarmManager::setAlarmTime(uint8_t index, uint8_t hour, uint8_t minute) {
    if (index >= NUM_ALARMS) return;
    alarms[index].hour = (hour < 24) ? hour : 23;
    alarms[index].minute = (minute < 60) ? minute : 59;
}

void AlarmManager::setAlarmDate(uint8_t index, uint8_t date, uint8_t month, uint8_t year) {
    if (index >= NUM_ALARMS) return;
    alarms[index].date = (date >= 1 && date <= 31) ? date : 1;
    alarms[index].month = (month >= 1 && month <= 12) ? month : 1;
    alarms[index].year = year;
}

void AlarmManager::setAlarmConfig(uint8_t index, const Alarm& config) {
    if (index >= NUM_ALARMS) return;

    Alarm sanitized = config;
    sanitized.hour %= 24;
    sanitized.minute %= 60;
    sanitized.month = (sanitized.month >= 1 && sanitized.month <= 12) ? sanitized.month : 1;
    sanitized.date = (sanitized.date >= 1 && sanitized.date <= 31) ? sanitized.date : 1;
    sanitized.toneIndex %= 3;
    if (sanitized.snoozeDelayMin < 5) sanitized.snoozeDelayMin = 5;
    if (sanitized.snoozeDelayMin > 15) sanitized.snoozeDelayMin = 15;
    if (sanitized.maxSnoozeCount > 10) sanitized.maxSnoozeCount = 10;
    if (!(sanitized.autoSilenceMin == 0 || sanitized.autoSilenceMin == 15 ||
          sanitized.autoSilenceMin == 30 || sanitized.autoSilenceMin == 60)) {
        sanitized.autoSilenceMin = 15;
    }
    sanitized.currentSnoozeCount = 0;
    sanitized.snoozeStartTimeMs = 0;
    sanitized.ringStartTimeMs = 0;

    // Do not let editing overwrite a currently sounding alarm unexpectedly;
    // the new state will take effect when it is saved.
    if (sanitized.state == ALARM_RINGING || sanitized.state == ALARM_SNOOZED) {
        sanitized.state = ALARM_READY;
    }

    alarms[index] = sanitized;
    persistenceRequest = true;

    if (activeRingingIndex == index && alarms[index].state != ALARM_RINGING) {
        setBuzzerFrequency(2500, false);
        activeRingingIndex = -1;
        promotePendingRingingAlarm();
    }
}

void AlarmManager::toggleAlarm(uint8_t index, bool active) {
    if (index >= NUM_ALARMS) return;

    if (active) {
        if (alarms[index].state == ALARM_OFF) {
            alarms[index].state = ALARM_READY;
            alarms[index].currentSnoozeCount = 0;
        }
    } else {
        alarms[index].state = ALARM_OFF;
        alarms[index].currentSnoozeCount = 0;
        alarms[index].snoozeStartTimeMs = 0;
        alarms[index].ringStartTimeMs = 0;

        if (activeRingingIndex == index) {
            setBuzzerFrequency(2500, false);
            activeRingingIndex = -1;
            promotePendingRingingAlarm();
        }
    }
    persistenceRequest = true;
}

bool AlarmManager::isRinging() const {
    return activeRingingIndex >= 0;
}

bool AlarmManager::consumePersistenceRequest() {
    const bool requested = persistenceRequest;
    persistenceRequest = false;
    return requested;
}

int AlarmManager::getActiveRingingIndex() const {
    return activeRingingIndex;
}
