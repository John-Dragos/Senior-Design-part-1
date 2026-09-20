#include "storage_manager.h"
#include "config.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

namespace {
static const char* TAG = "StorageManager";
}

bool StorageManager::begin() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NEW_VERSION_FOUND || err == ESP_ERR_NVS_NO_FREE_PAGES) {
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(err));
            return false;
        }
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

bool StorageManager::saveAll(const Alarm alarms[NUM_ALARMS],
                             bool use12Hour,
                             int8_t dstOffsetHours,
                             bool autoBrightness,
                             uint8_t manualBrightnessLevel) {
    PersistedState state = {};
    state.magic = NVS_MAGIC;
    state.version = NVS_VERSION;
    state.use12Hour = use12Hour ? 1 : 0;
    state.dstOffsetHours = dstOffsetHours;
    state.autoBrightness = autoBrightness ? 1 : 0;
    state.manualBrightnessLevel = manualBrightnessLevel;

    for (int i = 0; i < NUM_ALARMS; ++i) {
        const Alarm& src = alarms[i];
        PersistedAlarm& dst = state.alarm[i];
        dst.enabled = (src.state != ALARM_OFF) ? 1 : 0;
        dst.type = static_cast<uint8_t>(src.type);
        dst.hour = src.hour;
        dst.minute = src.minute;
        dst.date = src.date;
        dst.month = src.month;
        dst.year = src.year;
        dst.toneIndex = src.toneIndex;
        dst.snoozeDelayMin = src.snoozeDelayMin;
        dst.maxSnoozeCount = src.maxSnoozeCount;
        dst.autoSilenceMin = src.autoSilenceMin;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_blob(handle, "config", &state, sizeof(state));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS save failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool StorageManager::loadAll(Alarm alarms[NUM_ALARMS],
                             bool &use12Hour,
                             int8_t &dstOffsetHours,
                             bool &autoBrightness,
                             uint8_t &manualBrightnessLevel) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    PersistedState state = {};
    size_t size = sizeof(state);
    err = nvs_get_blob(handle, "config", &state, &size);
    nvs_close(handle);

    if (err != ESP_OK || size != sizeof(state) ||
        state.magic != NVS_MAGIC || state.version != NVS_VERSION) {
        return false;
    }

    use12Hour = state.use12Hour != 0;
    dstOffsetHours = (state.dstOffsetHours < -1 || state.dstOffsetHours > 1)
                         ? 0 : state.dstOffsetHours;
    autoBrightness = state.autoBrightness != 0;
    manualBrightnessLevel =
        (state.manualBrightnessLevel >= 3) ? 2 : state.manualBrightnessLevel;

    for (int i = 0; i < NUM_ALARMS; ++i) {
        const PersistedAlarm& src = state.alarm[i];
        Alarm& dst = alarms[i];

        dst.type = (src.type == ALARM_DATE) ? ALARM_DATE : ALARM_DAILY;
        dst.state = src.enabled ? ALARM_READY : ALARM_OFF;
        dst.hour = src.hour % 24;
        dst.minute = src.minute % 60;
        dst.date = (src.date >= 1 && src.date <= 31) ? src.date : 1;
        dst.month = (src.month >= 1 && src.month <= 12) ? src.month : 1;
        dst.year = src.year;
        dst.toneIndex = src.toneIndex % 3;
        dst.snoozeDelayMin = src.snoozeDelayMin;
        if (dst.snoozeDelayMin < 5) dst.snoozeDelayMin = 5;
        if (dst.snoozeDelayMin > 15) dst.snoozeDelayMin = 15;
        dst.maxSnoozeCount = (src.maxSnoozeCount <= 10) ? src.maxSnoozeCount : 10;
        dst.currentSnoozeCount = 0;
        dst.autoSilenceMin =
            (src.autoSilenceMin == 0 || src.autoSilenceMin == 15 ||
             src.autoSilenceMin == 30 || src.autoSilenceMin == 60)
                ? src.autoSilenceMin : 15;
        dst.snoozeStartTimeMs = 0;
        dst.ringStartTimeMs = 0;
    }

    return true;
}
