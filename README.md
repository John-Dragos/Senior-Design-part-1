# ESP32-S3 Non-Blocking Alarm Clock

This project is a complete replacement for the code pasted into the original PDF. It keeps the existing hardware pin assignments and the legacy ESP-IDF I2C driver used by the original project.

## Hardware pin assignments retained from the original code

| Hardware | GPIO |
|---|---:|
| OLED + DS3231 SDA | 8 |
| OLED + DS3231 SCL | 9 |
| Rotary encoder CLK | 4 |
| Rotary encoder DT | 5 |
| Rotary encoder SW | 6 |
| Buzzer | 7 |
| Pushbutton 1 | 10 |
| Pushbutton 2 | 11 |
| Pushbutton 3 | 12 |
| Photoresistor | 1 (ADC1 channel 0) |

OLED address is 0x3C and DS3231 address is 0x68, matching the submitted code.

## Required functions implemented

- DS3231 hardware timekeeping with seconds, date, year, and calculated day-of-week.
- Manual time setting and manual date setting.
- 12-hour / 24-hour display selection without changing the RTC's baseline 24-hour data.
- DST status of -1, 0, or +1 hour, with the RTC moved only by the difference between the old and new DST state.
- Three independent alarms.
- Alarm 1 default is daily; alarm 2 is date-based; alarm 3 is daily.
- Three distinct buzzer tone patterns.
- Visual alarm notification through OLED inversion/flashing.
- Manual silence and snooze.
- Snooze delay 5-15 minutes in one-minute increments.
- Maximum snooze count 1-10, with 0 meaning unlimited.
- Auto silence 15/30/60 minutes, with 0 meaning indefinite.
- Automatic photoresistor-controlled brightness.
- Manual brightness with three levels.
- OLED redraw only when necessary in normal operation, with hardware inversion used for alarm flashing.
- All configuration options survive power cycling through NVS.
- RTC time/date survive power cycling through the DS3231 backup coin cell.
- No delay() calls and no busy-wait alarm timers.

## User interface

### Normal clock

- Encoder click: open configuration menu.
- Button 1: quick toggle 12/24-hour format.
- Button 2: quick toggle Alarm 1 on/off.
- Button 3: quick toggle Auto/Manual brightness.

### While an alarm is ringing

- Button 2: snooze.
- Button 1 or Button 3: silence current ringing alarm.
- Encoder click: silence current ringing alarm.

When multiple alarms fire together, the alarm manager keeps each alarm's state and presents them through the single buzzer channel one at a time; silencing the active alarm promotes another ringing alarm.

### Configuration menu

Rotate the encoder to choose: TIME, DATE, ALARM, FORMAT, DST, DIMMODE, BRIGHT.

Encoder click enters an item. Button 1 is back/cancel. Buttons 2 and 3 act as decrement/increment inside an editor.

Alarm editing provides:

- Enable/disable
- Daily/date type
- Hour/minute
- Date fields for date-based alarms
- Tone 1/2/3
- Snooze delay 5-15 minutes
- Maximum snoozes 0-10 (0 = unlimited)
- Automatic silence 15/30/60 minutes or INF

## Important build notes

1. The original source used `forceInit = true` and wrote a hard-coded date/time at boot. That has been removed. The new code never overwrites the DS3231 on startup.
2. The DS3231 backup coin cell must be installed correctly for the RTC time/date to survive power loss.
3. The code assumes the photoresistor is connected as a simple voltage divider whose output is on GPIO1. The constant `PHOTORESISTOR_BRIGHT_IS_HIGH` in `config.h` can be changed if your particular divider produces the opposite ADC direction.
4. The OLED rotation is preserved from the original project. If your physical display is already mounted upright and the text is backwards/upside-down, remove the `rotateBuffer180()` calls in `display_manager.cc`.
5. The code uses the ESP-IDF legacy `driver/i2c.h` and `driver/adc.h` APIs because that is what the original project used. This is intentional for compatibility with an existing course setup.

## Build

This project is configured for the ESP32-S3. The included `sdkconfig.defaults` sets `CONFIG_IDF_TARGET="esp32s3"`, but an existing `build/` directory or `sdkconfig` from an earlier ESP32 target can override it. ESP-IDF documents `idf.py set-target esp32s3` as the command that clears the old build/configuration and initializes the project for the selected target.

From the project root, use:

```text
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

If you are using the ESP-IDF VS Code extension, make sure the selected target is `esp32s3` before building/flashing. Do not reuse a build directory that was configured for `esp32`.

The code was reviewed for the logic and API patterns in the supplied source. An ESP32-S3 board, the exact ESP-IDF version, OLED module, and photoresistor wiring were not available here, so final verification still requires compiling against your course ESP-IDF environment and testing on the assembled hardware.

## Hardware bring-up / acceptance test

1. Power on and confirm the OLED shows HH:MM:SS, day of week, and date. Leave it running for several minutes and verify seconds advance normally.
2. Enter MENU -> TIME and set a time one or two minutes in the future. Confirm the RTC continues after leaving the menu.
3. Enter MENU -> DATE and set a known date. Verify the correct day of week appears.
4. MENU -> FORMAT toggles 12-hour/24-hour display. Verify midnight displays 12:xx AM and noon 12:xx PM in 12-hour mode.
5. MENU -> DST cycles NORMAL, +1 HOUR, -1 HOUR. Check the displayed time changes by exactly one hour and does not change again when selecting the same setting again.
6. MENU -> DIMMODE selects AUTO or MANUAL. In MANUAL mode, MENU -> BRIGHTNESS previews LOW/MEDIUM/HIGH immediately as the encoder is rotated.
7. For AUTO brightness, shine a light directly at the photoresistor and then cover it. The OLED should change contrast. If the direction is reversed, change PHOTORESISTOR_BRIGHT_IS_HIGH in config.h.
8. Configure ALARM 1 as DAILY at a time 1-2 minutes ahead. Confirm buzzer + display flashing. BTN2 snoozes; BTN1, BTN3, or the encoder button silence the active alarm.
9. Configure ALARM 2 as DATE at a test date/time. Confirm it does not trigger on another date.
10. Configure ALARM 3 separately and select a different tone. Verify the three tone choices sound different.
11. Test snooze delays at 5 and 15 minutes (or use a temporarily shorter test value during development, then restore the required 5-15 minute range).
12. Test max snoozes at 1 and 2 and auto-silence at 15/30/60 minutes. Verify 0 means indefinite.
13. Power-cycle the clock and verify time/date, alarms, format, DST setting, and dimming settings remain saved. The DS3231 must not be overwritten with a compile-time date/time.
