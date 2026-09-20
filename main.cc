#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "driver/adc.h"
#include "esp_log.h"
#include "esp_timer.h"
}

#include "config.h"
#include "time_manager.h"
#include "input_manager.h"
#include "alarm_manager.h"
#include "display_manager.h"
#include "storage_manager.h"

TimeManager timeManager;
InputManager inputManager;
AlarmManager alarmManager;
DisplayManager displayManager;
StorageManager storageManager;

namespace {
static const char* TAG = "Main";

// ----------------------------- UI states -----------------------------
enum UiState {
    UI_CLOCK = 0,
    UI_MENU,
    UI_SET_TIME,
    UI_SET_DATE,
    UI_ALARM_SELECT,
    UI_ALARM_EDIT,
    UI_FORMAT,
    UI_DST,
    UI_DIM_MODE,
    UI_BRIGHTNESS
};

UiState uiState = UI_CLOCK;
uint8_t menuIndex = 0;
constexpr uint8_t MENU_COUNT = 7;
const char* MENU_ITEMS[MENU_COUNT] = {
    "TIME", "DATE", "ALARM", "FORMAT", "DST", "DIMMODE", "BRIGHT"
};

uint8_t editField = 0;
uint8_t alarmIndex = 0;
Alarm tempAlarm;

uint8_t tempHour = 0;
uint8_t tempMinute = 0;
uint8_t tempDate = 1;
uint8_t tempMonth = 1;
uint8_t tempYear = 26;

bool use12HourFormat = true;
int8_t dstOffsetHours = 0; // -1, 0, +1; persisted status, not auto-applied at boot.
// Tracks the DST offset physically applied to the RTC so changing the setting
// later moves the RTC by only the difference, not by another whole hour.
int8_t rtcAppliedDstOffsetHours = 0;
bool autoBrightness = true;
uint8_t manualBrightnessLevel = 2; // 0=low, 1=medium, 2=high

bool editUse12HourFormat = true;
int8_t editDstOffsetHours = 0;
bool editAutoBrightness = true;
uint8_t editManualBrightnessLevel = 2;
bool brightnessEditPreview = false;

bool uiDirty = true;
int lastShownSecond = -1;
uint64_t lastPhotoSampleMs = 0;

uint8_t wrapValue(int value, int minValue, int maxValue) {
    const int range = maxValue - minValue + 1;
    while (value < minValue) value += range;
    while (value > maxValue) value -= range;
    return static_cast<uint8_t>(value);
}

int8_t wrapSignedValue(int value, int minValue, int maxValue) {
    const int range = maxValue - minValue + 1;
    while (value < minValue) value += range;
    while (value > maxValue) value -= range;
    return static_cast<int8_t>(value);
}

bool anyAlarmEnabled() {
    for (uint8_t i = 0; i < NUM_ALARMS; ++i) {
        const Alarm& alarm = alarmManager.getAlarm(i);
        if (alarm.state != ALARM_OFF) return true;
    }
    return false;
}

void saveConfiguration() {
    if (!storageManager.saveAll(&alarmManager.getAlarm(0), use12HourFormat,
                                dstOffsetHours, autoBrightness, manualBrightnessLevel)) {
        ESP_LOGE(TAG, "Could not save configuration to NVS");
    }
}

void initPhotoresistor() {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(PHOTORESISTOR_ADC_CHANNEL, ADC_ATTEN_DB_12);
}

uint8_t photoToBrightness(uint16_t raw) {
    // Three clearly visible ranges. If your particular voltage-divider wiring
    // produces LOWER ADC values in bright light, set PHOTORESISTOR_BRIGHT_IS_HIGH
    // to 0 in config.h.
#if PHOTORESISTOR_BRIGHT_IS_HIGH
    if (raw < 1000) return 28;
    if (raw < 2500) return 100;
    return 220;
#else
    if (raw > 3000) return 28;
    if (raw > 1500) return 100;
    return 220;
#endif
}

void updateBrightness(uint64_t nowMs) {
    static uint8_t lastAppliedLevel = 255;

    // While editing BRIGHTNESS, temporarily show the selected manual level
    // regardless of the saved AUTO/MANUAL mode so LOW/MEDIUM/HIGH can be
    // compared immediately. Cancel restores the previous mode.
    if (brightnessEditPreview) {
        const uint8_t levels[3] = {32, 128, 255};
        const uint8_t level = (editManualBrightnessLevel < 3) ? editManualBrightnessLevel : 2;
        if (level != lastAppliedLevel) {
            displayManager.setBrightness(levels[level]);
            lastAppliedLevel = level;
        }
        return;
    }

    if (!autoBrightness) {
        const uint8_t levels[3] = {32, 128, 255};
        const uint8_t level = (manualBrightnessLevel < 3) ? manualBrightnessLevel : 2;
        if (level != lastAppliedLevel) {
            displayManager.setBrightness(levels[level]);
            lastAppliedLevel = level;
        }
        return;
    }

    if (nowMs - lastPhotoSampleMs < AUTO_BRIGHTNESS_SAMPLE_MS) {
        return;
    }
    lastPhotoSampleMs = nowMs;

    static uint16_t filtered = 0;
    const int raw = adc1_get_raw(PHOTORESISTOR_ADC_CHANNEL);
    if (filtered == 0) filtered = static_cast<uint16_t>(raw);
    else filtered = static_cast<uint16_t>((filtered * 3U + static_cast<uint16_t>(raw)) / 4U);

    const uint8_t autoLevel = photoToBrightness(filtered);
    displayManager.setBrightness(autoLevel);
    lastAppliedLevel = 255; // photo levels are not the 3 manual-level values
}

void enterMenu() {
    uiState = UI_MENU;
    uiDirty = true;
}

void startTimeEdit() {
    uint8_t h, m, s, day, date, month, year;
    timeManager.getCurrentTime(h, m, s, day, date, month, year);
    (void)s;
    (void)day;
    tempHour = h;
    tempMinute = m;
    editField = 0;
    uiState = UI_SET_TIME;
    uiDirty = true;
}

void startDateEdit() {
    uint8_t h, m, s, day, date, month, year;
    timeManager.getCurrentTime(h, m, s, day, date, month, year);
    (void)h;
    (void)m;
    (void)s;
    (void)day;
    tempDate = date;
    tempMonth = month;
    tempYear = year;
    editField = 0;
    uiState = UI_SET_DATE;
    uiDirty = true;
}

void startAlarmEdit() {
    tempAlarm = alarmManager.getAlarm(alarmIndex);
    editField = 0;
    uiState = UI_ALARM_EDIT;
    uiDirty = true;
}

void startSimpleSettingEdit(UiState state) {
    editUse12HourFormat = use12HourFormat;
    editDstOffsetHours = dstOffsetHours;
    editAutoBrightness = autoBrightness;
    editManualBrightnessLevel = manualBrightnessLevel;
    brightnessEditPreview = (state == UI_BRIGHTNESS);
    uiState = state;
    uiDirty = true;
}

uint8_t alarmFieldCount() {
    // Daily: enable, type, hour, minute, tone, snooze, max snoozes, silence.
    // Date: insert month/date/year before tone.
    return (tempAlarm.type == ALARM_DATE) ? 11 : 8;
}

const char* alarmFieldName(uint8_t field) {
    if (tempAlarm.type == ALARM_DAILY) {
        static const char* names[] = {
            "ENABLE", "TYPE", "HOUR", "MIN", "TONE", "SNOOZE", "MAXSNOOZE", "SILENCE"
        };
        return names[field < 8 ? field : 7];
    }

    static const char* names[] = {
        "ENABLE", "TYPE", "HOUR", "MIN", "MONTH", "DATE", "YEAR", "TONE", "SNOOZE", "MAXSNOOZE", "SILENCE"
    };
    return names[field < 11 ? field : 10];
}

void adjustAlarmField(int delta) {
    if (delta == 0) return;

    if (tempAlarm.type == ALARM_DAILY) {
        switch (editField) {
            case 0:
                tempAlarm.state = (delta > 0) ? ALARM_READY : ALARM_OFF;
                break;
            case 1:
                tempAlarm.type = (delta > 0) ? ALARM_DATE : ALARM_DAILY;
                break;
            case 2:
                tempAlarm.hour = wrapValue(tempAlarm.hour + delta, 0, 23);
                break;
            case 3:
                tempAlarm.minute = wrapValue(tempAlarm.minute + delta, 0, 59);
                break;
            case 4:
                tempAlarm.toneIndex = wrapValue(tempAlarm.toneIndex + delta, 0, 2);
                break;
            case 5:
                tempAlarm.snoozeDelayMin = wrapValue(tempAlarm.snoozeDelayMin + delta, 5, 15);
                break;
            case 6:
                tempAlarm.maxSnoozeCount = wrapValue(tempAlarm.maxSnoozeCount + delta, 0, 10);
                break;
            case 7: {
                static const uint16_t choices[] = {15, 30, 60, 0};
                int idx = 0;
                for (int i = 0; i < 4; ++i) if (choices[i] == tempAlarm.autoSilenceMin) idx = i;
                idx = (idx + delta) % 4;
                if (idx < 0) idx += 4;
                tempAlarm.autoSilenceMin = choices[idx];
                break;
            }
            default:
                break;
        }
        return;
    }

    switch (editField) {
        case 0:
            tempAlarm.state = (delta > 0) ? ALARM_READY : ALARM_OFF;
            break;
        case 1:
            tempAlarm.type = (delta > 0) ? ALARM_DATE : ALARM_DAILY;
            break;
        case 2:
            tempAlarm.hour = wrapValue(tempAlarm.hour + delta, 0, 23);
            break;
        case 3:
            tempAlarm.minute = wrapValue(tempAlarm.minute + delta, 0, 59);
            break;
        case 4:
            tempAlarm.month = wrapValue(tempAlarm.month + delta, 1, 12);
            if (tempAlarm.date > TimeManager::daysInMonth(tempAlarm.year, tempAlarm.month)) {
                tempAlarm.date = TimeManager::daysInMonth(tempAlarm.year, tempAlarm.month);
            }
            break;
        case 5: {
            const uint8_t dim = TimeManager::daysInMonth(tempAlarm.year, tempAlarm.month);
            tempAlarm.date = wrapValue(tempAlarm.date + delta, 1, dim);
            break;
        }
        case 6:
            tempAlarm.year = wrapValue(tempAlarm.year + delta, 0, 99);
            if (tempAlarm.date > TimeManager::daysInMonth(tempAlarm.year, tempAlarm.month)) {
                tempAlarm.date = TimeManager::daysInMonth(tempAlarm.year, tempAlarm.month);
            }
            break;
        case 7:
            tempAlarm.toneIndex = wrapValue(tempAlarm.toneIndex + delta, 0, 2);
            break;
        case 8:
            tempAlarm.snoozeDelayMin = wrapValue(tempAlarm.snoozeDelayMin + delta, 5, 15);
            break;
        case 9:
            tempAlarm.maxSnoozeCount = wrapValue(tempAlarm.maxSnoozeCount + delta, 0, 10);
            break;
        case 10: {
            static const uint16_t choices[] = {15, 30, 60, 0};
            int idx = 0;
            for (int i = 0; i < 4; ++i) if (choices[i] == tempAlarm.autoSilenceMin) idx = i;
            idx = (idx + delta) % 4;
            if (idx < 0) idx += 4;
            tempAlarm.autoSilenceMin = choices[idx];
            break;
        }
        default:
            break;
    }
}

void saveAlarmEdit() {
    alarmManager.setAlarmConfig(alarmIndex, tempAlarm);
    saveConfiguration();
    uiState = UI_ALARM_SELECT;
    uiDirty = true;
}

void adjustSetTime(int delta) {
    if (editField == 0) tempHour = wrapValue(tempHour + delta, 0, 23);
    else tempMinute = wrapValue(tempMinute + delta, 0, 59);
}

void adjustSetDate(int delta) {
    if (editField == 0) {
        tempMonth = wrapValue(tempMonth + delta, 1, 12);
    } else if (editField == 1) {
        const uint8_t dim = TimeManager::daysInMonth(tempYear, tempMonth);
        tempDate = wrapValue(tempDate + delta, 1, dim);
    } else {
        tempYear = wrapValue(tempYear + delta, 0, 99);
    }

    const uint8_t dim = TimeManager::daysInMonth(tempYear, tempMonth);
    if (tempDate > dim) tempDate = dim;
}

void renderUi(uint8_t h, uint8_t m, uint8_t s, uint8_t day,
              uint8_t date, uint8_t month, uint8_t year) {
    char title[24];
    char value[32];
    char hint[32];

    switch (uiState) {
        case UI_CLOCK:
            displayManager.renderClockFace(
                h, m, s, day, date, month, year,
                use12HourFormat, anyAlarmEnabled(), dstOffsetHours);
            break;

        case UI_MENU:
            displayManager.renderMenu(MENU_ITEMS[menuIndex], menuIndex, MENU_COUNT);
            break;

        case UI_SET_TIME:
            snprintf(title, sizeof(title), "SET TIME %u/2", editField + 1);
            snprintf(value, sizeof(value), editField == 0 ? "HOUR %02u" : "MIN %02u",
                     editField == 0 ? tempHour : tempMinute);
            displayManager.renderTitleValue(title, value, "ROTATE CLICK B1BACK");
            break;

        case UI_SET_DATE:
            snprintf(title, sizeof(title), "SET DATE %u/3", editField + 1);
            if (editField == 0) snprintf(value, sizeof(value), "MONTH %02u", tempMonth);
            else if (editField == 1) snprintf(value, sizeof(value), "DAY %02u", tempDate);
            else snprintf(value, sizeof(value), "YEAR 20%02u", tempYear);
            displayManager.renderTitleValue(title, value, "ROTATE CLICK B1BACK");
            break;

        case UI_ALARM_SELECT:
            snprintf(title, sizeof(title), "SELECT ALARM");
            snprintf(value, sizeof(value), "ALARM %u", alarmIndex + 1);
            displayManager.renderTitleValue(title, value, "CLICK EDIT B1BACK");
            break;

        case UI_ALARM_EDIT:
            snprintf(title, sizeof(title), "ALM%u %s", alarmIndex + 1, alarmFieldName(editField));
            switch (editField) {
                case 0: snprintf(value, sizeof(value), "%s", tempAlarm.state == ALARM_OFF ? "OFF" : "ON"); break;
                case 1: snprintf(value, sizeof(value), "%s", tempAlarm.type == ALARM_DAILY ? "DAILY" : "DATE"); break;
                case 2: snprintf(value, sizeof(value), "HOUR %02u", tempAlarm.hour); break;
                case 3: snprintf(value, sizeof(value), "MIN %02u", tempAlarm.minute); break;
                case 4: snprintf(value, sizeof(value), "MONTH %02u", tempAlarm.month); break;
                case 5: snprintf(value, sizeof(value), "DATE %02u", tempAlarm.date); break;
                case 6: snprintf(value, sizeof(value), "YEAR 20%02u", tempAlarm.year); break;
                case 7: snprintf(value, sizeof(value), "TONE %u", tempAlarm.toneIndex + 1); break;
                case 8: snprintf(value, sizeof(value), "SNOOZE %uM", tempAlarm.snoozeDelayMin); break;
                case 9:
                    if (tempAlarm.maxSnoozeCount == 0) snprintf(value, sizeof(value), "MAX INF");
                    else snprintf(value, sizeof(value), "MAX %u", tempAlarm.maxSnoozeCount);
                    break;
                case 10:
                    if (tempAlarm.autoSilenceMin == 0) snprintf(value, sizeof(value), "SIL INF");
                    else snprintf(value, sizeof(value), "SIL %uM", tempAlarm.autoSilenceMin);
                    break;
                default: snprintf(value, sizeof(value), "ERR"); break;
            }
            snprintf(hint, sizeof(hint), "ROTATE CLICK B1CANCEL");
            displayManager.renderTitleValue(title, value, hint);
            break;

        case UI_FORMAT:
            snprintf(title, sizeof(title), "FORMAT");
            snprintf(value, sizeof(value), editUse12HourFormat ? "12 HOUR AMPM" : "24 HOUR");
            displayManager.renderTitleValue(title, value, "ROTATE CLICK B1BACK");
            break;

        case UI_DST:
            snprintf(title, sizeof(title), "DST OFFSET");
            if (editDstOffsetHours > 0) snprintf(value, sizeof(value), "+1 HOUR");
            else if (editDstOffsetHours < 0) snprintf(value, sizeof(value), "-1 HOUR");
            else snprintf(value, sizeof(value), "NORMAL");
            displayManager.renderTitleValue(title, value, "ROTATE CLICK B1BACK");
            break;

        case UI_DIM_MODE:
            snprintf(title, sizeof(title), "DIM MODE");
            snprintf(value, sizeof(value), editAutoBrightness ? "AUTO" : "MANUAL");
            displayManager.renderTitleValue(title, value, "ROTATE CLICK B1BACK");
            break;

        case UI_BRIGHTNESS:
            snprintf(title, sizeof(title), "BRIGHTNESS");
            if (editManualBrightnessLevel == 0) snprintf(value, sizeof(value), "LOW");
            else if (editManualBrightnessLevel == 1) snprintf(value, sizeof(value), "MEDIUM");
            else snprintf(value, sizeof(value), "HIGH");
            displayManager.renderTitleValue(title, value, "ROTATE LIVE CLICK SAVE");
            break;

        default:
            break;
    }
}

void handleEncoderRotation(int delta) {
    if (delta == 0) return;

    switch (uiState) {
        case UI_CLOCK:
            // Rotation is intentionally unused on the clock face; this avoids
            // accidental settings changes while the device is being carried.
            break;

        case UI_MENU:
            menuIndex = wrapValue(menuIndex + delta, 0, MENU_COUNT - 1);
            uiDirty = true;
            break;

        case UI_SET_TIME:
            adjustSetTime(delta);
            uiDirty = true;
            break;

        case UI_SET_DATE:
            adjustSetDate(delta);
            uiDirty = true;
            break;

        case UI_ALARM_SELECT:
            alarmIndex = wrapValue(alarmIndex + delta, 0, NUM_ALARMS - 1);
            uiDirty = true;
            break;

        case UI_ALARM_EDIT:
            adjustAlarmField(delta);
            uiDirty = true;
            break;

        case UI_FORMAT:
            editUse12HourFormat = !editUse12HourFormat;
            uiDirty = true;
            break;

        case UI_DST:
            editDstOffsetHours = wrapSignedValue(editDstOffsetHours + delta, -1, 1);
            uiDirty = true;
            break;

        case UI_DIM_MODE:
            editAutoBrightness = !editAutoBrightness;
            uiDirty = true;
            break;

        case UI_BRIGHTNESS:
            editManualBrightnessLevel = wrapValue(editManualBrightnessLevel + delta, 0, 2);
            // Preview each manual level immediately without changing the saved
            // AUTO/MANUAL mode until the user presses the encoder button.
            brightnessEditPreview = true;
            uiDirty = true;
            break;
    }
}

void handleEncoderClick() {
    // Encoder click has priority as a convenient alarm stop.
    if (alarmManager.isRinging()) {
        alarmManager.stopActive();
        uiState = UI_CLOCK;
        uiDirty = true;
        return;
    }

    switch (uiState) {
        case UI_CLOCK:
            enterMenu();
            break;

        case UI_MENU:
            switch (menuIndex) {
                case 0: startTimeEdit(); break;
                case 1: startDateEdit(); break;
                case 2: uiState = UI_ALARM_SELECT; uiDirty = true; break;
                case 3: startSimpleSettingEdit(UI_FORMAT); break;
                case 4: startSimpleSettingEdit(UI_DST); break;
                case 5: startSimpleSettingEdit(UI_DIM_MODE); break;
                case 6: startSimpleSettingEdit(UI_BRIGHTNESS); break;
                default: break;
            }
            break;

        case UI_SET_TIME: {
            if (editField < 1) {
                ++editField;
                uiDirty = true;
            } else {
                uint8_t h, m, s, day, date, month, year;
                timeManager.getCurrentTime(h, m, s, day, date, month, year);
                (void)h;
                (void)m;
                (void)day;
                if (timeManager.setDateTime(tempHour, tempMinute, 0,
                                            date, month, year)) {
                    uiState = UI_CLOCK;
                }
                uiDirty = true;
            }
            break;
        }

        case UI_SET_DATE: {
            if (editField < 2) {
                ++editField;
                uiDirty = true;
            } else {
                uint8_t h, m, s, day, date, month, year;
                timeManager.getCurrentTime(h, m, s, day, date, month, year);
                (void)day;
                (void)date;
                (void)month;
                (void)year;
                if (timeManager.setDateTime(h, m, s,
                                            tempDate, tempMonth, tempYear)) {
                    uiState = UI_CLOCK;
                }
                uiDirty = true;
            }
            break;
        }

        case UI_ALARM_SELECT:
            startAlarmEdit();
            break;

        case UI_ALARM_EDIT:
            if (editField + 1 < alarmFieldCount()) {
                ++editField;
            } else {
                saveAlarmEdit();
                break;
            }
            uiDirty = true;
            break;

        case UI_FORMAT:
            use12HourFormat = editUse12HourFormat;
            saveConfiguration();
            uiState = UI_CLOCK;
            uiDirty = true;
            break;

        case UI_DST: {
            dstOffsetHours = editDstOffsetHours;
            // Adjust RTC only by the difference so selecting +1 twice never
            // adds another hour.
            const int8_t delta = dstOffsetHours - rtcAppliedDstOffsetHours;
            if (delta != 0 && timeManager.adjustHours(delta)) {
                rtcAppliedDstOffsetHours = dstOffsetHours;
            }
            saveConfiguration();
            uiState = UI_CLOCK;
            uiDirty = true;
            break;
        }

        case UI_DIM_MODE:
            autoBrightness = editAutoBrightness;
            saveConfiguration();
            uiState = UI_CLOCK;
            uiDirty = true;
            break;

        case UI_BRIGHTNESS:
            manualBrightnessLevel = editManualBrightnessLevel;
            autoBrightness = editAutoBrightness;
            brightnessEditPreview = false;
            saveConfiguration();
            uiState = UI_CLOCK;
            uiDirty = true;
            break;

        default:
            break;
    }
}

void handleButtonClicks() {
    const bool b1 = inputManager.wasButtonClicked(BTN_1);
    const bool b2 = inputManager.wasButtonClicked(BTN_2);
    const bool b3 = inputManager.wasButtonClicked(BTN_3);

    if (alarmManager.isRinging()) {
        // Required manual silence + snooze. BTN2 snoozes; BTN1/BTN3 silence.
        if (b2) {
            alarmManager.snoozeActive();
            uiDirty = true;
        }
        if (b1 || b3) {
            alarmManager.stopActive();
            uiDirty = true;
        }
        return;
    }

    switch (uiState) {
        case UI_CLOCK:
            if (b1) {
                use12HourFormat = !use12HourFormat;
                saveConfiguration();
                uiDirty = true;
            }
            if (b2) {
                Alarm& alarm0 = alarmManager.getAlarm(0);
                alarmManager.toggleAlarm(0, alarm0.state == ALARM_OFF);
                saveConfiguration();
                uiDirty = true;
            }
            if (b3) {
                autoBrightness = !autoBrightness;
                saveConfiguration();
                uiDirty = true;
            }
            break;

        case UI_MENU:
        case UI_SET_TIME:
        case UI_SET_DATE:
        case UI_ALARM_SELECT:
        case UI_ALARM_EDIT:
        case UI_FORMAT:
        case UI_DST:
        case UI_DIM_MODE:
        case UI_BRIGHTNESS:
            if (b1) {
                // Back/cancel for every configuration page. Restore temporary
                // simple-setting edits before leaving those pages.
                if (uiState == UI_FORMAT) {
                    use12HourFormat = editUse12HourFormat;
                } else if (uiState == UI_DST) {
                    editDstOffsetHours = dstOffsetHours;
                } else if (uiState == UI_DIM_MODE) {
                    autoBrightness = editAutoBrightness;
                } else if (uiState == UI_BRIGHTNESS) {
                    brightnessEditPreview = false;
                    autoBrightness = editAutoBrightness;
                }
                uiState = (uiState == UI_MENU) ? UI_CLOCK :
                          (uiState == UI_ALARM_SELECT) ? UI_MENU :
                          (uiState == UI_ALARM_EDIT) ? UI_ALARM_SELECT : UI_MENU;
                uiDirty = true;
                return;
            }
            if (b2) handleEncoderRotation(-1);
            if (b3) handleEncoderRotation(1);
            break;

        default:
            break;
    }
}

void applyStartupBrightness() {
    if (autoBrightness) {
        const int raw = adc1_get_raw(PHOTORESISTOR_ADC_CHANNEL);
        displayManager.setBrightness(photoToBrightness(static_cast<uint16_t>(raw)));
    } else {
        const uint8_t levels[3] = {28, 100, 220};
        displayManager.setBrightness(levels[manualBrightnessLevel]);
    }
}
}

extern "C" void app_main(void) {
    printf("Starting non-blocking ESP32-S3 Alarm Clock...\n");

    storageManager.begin();
    timeManager.begin();
    displayManager.begin();
    inputManager.begin();
    alarmManager.begin();
    initPhotoresistor();

    // Load all persistent user configuration after managers have initialized
    // their safe defaults. DS3231 time/date are intentionally NOT overwritten.
    if (!storageManager.loadAll(&alarmManager.getAlarm(0),
                                use12HourFormat, dstOffsetHours,
                                autoBrightness, manualBrightnessLevel)) {
        ESP_LOGI(TAG, "No saved configuration found; using defaults");
        saveConfiguration();
    }
    // The RTC already contains local wall-clock time. The persisted DST state
    // therefore describes what is already applied; do not shift it again at boot.
    rtcAppliedDstOffsetHours = dstOffsetHours;

    applyStartupBrightness();

    uint8_t h = 0, m = 0, s = 0, day = 1, date = 1, month = 1, year = 26;

    // The loop uses a short FreeRTOS yield, not delay(), and all application
    // timers are based on timestamps so no function blocks waiting for time.
    while (true) {
        timeManager.update();
        inputManager.update();

        timeManager.getCurrentTime(h, m, s, day, date, month, year);
        const bool wasRinging = alarmManager.isRinging();
        alarmManager.update(h, m, s, date, month, year);
        const bool nowRinging = alarmManager.isRinging();
        if (nowRinging && !wasRinging) {
            uiState = UI_CLOCK;
            uiDirty = true;
        }
        if (alarmManager.consumePersistenceRequest()) {
            saveConfiguration();
        }

        const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
        updateBrightness(nowMs);

        handleButtonClicks();
        handleEncoderRotation(inputManager.getEncoderDelta());
        if (inputManager.isEncoderButtonClicked()) {
            handleEncoderClick();
        }

        // Update the OLED once per RTC second in normal mode, and immediately
        // when the UI changes. The OLED retains its image between updates, so
        // there is no visible redraw flicker.
        if (uiState == UI_CLOCK) {
            if (uiDirty || static_cast<int>(s) != lastShownSecond) {
                renderUi(h, m, s, day, date, month, year);
                lastShownSecond = s;
                uiDirty = false;
            }
        } else if (uiDirty) {
            renderUi(h, m, s, day, date, month, year);
            uiDirty = false;
        }

        displayManager.processAlarmFlashing(alarmManager.isRinging());

        // This is a FreeRTOS scheduler yield, not a blocking delay used for
        // alarm logic. It keeps the task responsive while avoiding a busy loop.
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
