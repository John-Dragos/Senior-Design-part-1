# Alarm Clock Requirements Matrix

| Requirement | Implementation |
|---|---|
| Accurate timekeeping | `time_manager.cc` uses the DS3231 as the authoritative clock and reads it continuously. Manual time/date writes go back to the DS3231. No boot-time overwrite. |
| < 1 sec/day drift | The software does not replace or simulate the RTC clock. Actual drift is determined by the DS3231 module/crystal and must be measured on the assembled hardware. |
| Current time, date, day | Clock screen displays HH:MM:SS, weekday, MM/DD/YYYY. Weekday is calculated when a date is manually written. |
| Manual time/date set | TIME and DATE menu items edit each field with encoder rotation and save with encoder click. |
| 12/24 hour | FORMAT menu selects 12-hour with AM/PM or 24-hour. RTC remains stored as 24-hour baseline data. |
| DST +/- 1 hour | DST menu provides -1, NORMAL, +1. Only the difference from the currently applied offset is written to the RTC, so selecting the same state repeatedly does not add another hour. DST state is stored in NVS. |
| 3 independent alarms | Three complete `Alarm` objects are independently stored and edited. |
| Daily alarm | Alarm 1 defaults to daily and retains daily type as an option. |
| Date alarm | Alarm 2 defaults to date-based; date/month/year fields appear when DATE type is selected. |
| 3 tones | Tone 1/2/3 use different frequencies and pulse rates. |
| Visual alarm | OLED is hardware-inverted every 300 ms while ringing. |
| Manual silence | BTN1 or BTN3, or encoder click, stops the current alarm. A second simultaneously ringing alarm is promoted. |
| Snooze | BTN2 snoozes the current alarm. Countdown is timestamp-based, never `delay()`. |
| Snooze delay 5-15 min | Encoder editor clamps/wraps from 5 through 15 minutes in one-minute increments. |
| Snooze count 1-10 / infinite | Editor supports 1-10 plus 0 = unlimited. |
| Auto silence | Editor supports 15, 30, 60, and 0 = indefinite. |
| Auto dimming | GPIO1 is read as an ADC input and mapped into three OLED contrast ranges based on ambient light. |
| Manual brightness | Manual mode provides LOW, MEDIUM, HIGH. |
| Manual overrides auto | Auto sampling is disabled whenever manual mode is selected. |
| No flicker | Normal screen is transmitted once per RTC second or when UI changes; alarm flashing uses the OLED invert command instead of full redraws. |
| Responsive UI | Encoder + three pushbuttons have non-blocking debounce/event handling and a multi-page menu. |
| Power-cycle resilience | Alarm/settings/DST/format/brightness state are stored in NVS. RTC time/date remain in the DS3231 backup domain. |
| No blocking alarm logic | Alarm, snooze, flashing, dimming, debounce, and display refresh are timestamp/state based. The main task only yields with FreeRTOS `vTaskDelay`, and there are no `delay()` calls. |
