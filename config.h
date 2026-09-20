#pragma once

#include <stdint.h>

// ---------- I2C ----------
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000
#define DS3231_ADDR 0x68
#define SSD1306_ADDR 0x3C
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 9

// ---------- Rotary encoder ----------
#define PIN_ENC_CLK 4
#define PIN_ENC_DT 5
#define PIN_ENC_SW 6
#define ENCODER_REVERSED 0

// ---------- Pushbuttons ----------
#define PIN_BTN_1 10
#define PIN_BTN_2 11
#define PIN_BTN_3 12

// ---------- Outputs / analog input ----------
#define PIN_BUZZER 7
#define PIN_PHOTORESISTOR 1

// ESP32-S3 GPIO1 = ADC1 channel 0 on the normal GPIO/ADC mapping.
#define PHOTORESISTOR_ADC_CHANNEL ADC1_CHANNEL_0
#define PHOTORESISTOR_BRIGHT_IS_HIGH 1

// ---------- Alarm system ----------
#define NUM_ALARMS 3
#define ALARM_DEFAULT_HOUR 7
#define ALARM_DEFAULT_MINUTE 0

// ---------- Display ----------
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_FRAMEBUFFER_BYTES (OLED_WIDTH * OLED_HEIGHT / 8)

// ---------- UI timing ----------
#define INPUT_DEBOUNCE_MS 30
#define DISPLAY_REFRESH_MS 1000
#define FLASH_INTERVAL_MS 300
#define AUTO_BRIGHTNESS_SAMPLE_MS 250

// ---------- NVS ----------
#define NVS_NAMESPACE "alarm_clock"
#define NVS_MAGIC 0x41434C4Bu // "ACLK"
#define NVS_VERSION 2

// 13-bit LEDC PWM duty range: 0..8191.
#define BUZZER_DUTY 4096
