#include "input_manager.h"

extern "C" {
#include "driver/gpio.h"
#include "esp_timer.h"
}

void InputManager::begin() {
    uint64_t pinMask = 0;
    for (int pin : buttonPins) {
        pinMask |= (1ULL << pin);
    }
    pinMask |= (1ULL << PIN_ENC_CLK) | (1ULL << PIN_ENC_DT);

    gpio_config_t ioConf = {};
    ioConf.intr_type = GPIO_INTR_DISABLE;
    ioConf.mode = GPIO_MODE_INPUT;
    ioConf.pin_bit_mask = pinMask;
    ioConf.pull_up_en = GPIO_PULLUP_ENABLE;
    ioConf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&ioConf);

    const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;
    for (int i = 0; i < NUM_BUTTONS; ++i) {
        const bool pressed = (gpio_get_level(static_cast<gpio_num_t>(buttonPins[i])) == 0);
        buttonStates[i] = pressed;
        lastButtonStates[i] = pressed;
        buttonEventFlags[i] = false;
        lastDebounceTime[i] = nowMs;
    }

    const uint8_t clk = static_cast<uint8_t>(gpio_get_level(static_cast<gpio_num_t>(PIN_ENC_CLK)));
    const uint8_t dt = static_cast<uint8_t>(gpio_get_level(static_cast<gpio_num_t>(PIN_ENC_DT)));
    lastEncoderState = static_cast<uint8_t>((clk << 1) | dt);
    encoderAccumulator = 0;
    encoderDelta = 0;
}

void InputManager::update() {
    const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time()) / 1000ULL;

    // Debounced, active-low buttons.
    for (int i = 0; i < NUM_BUTTONS; ++i) {
        const bool reading =
            gpio_get_level(static_cast<gpio_num_t>(buttonPins[i])) == 0;

        if (reading != lastButtonStates[i]) {
            lastButtonStates[i] = reading;
            lastDebounceTime[i] = nowMs;
        }

        if (nowMs - lastDebounceTime[i] >= INPUT_DEBOUNCE_MS &&
            reading != buttonStates[i]) {
            buttonStates[i] = reading;
            if (reading) {
                buttonEventFlags[i] = true;
            }
        }
    }

    // Full quadrature state-machine decoding. Four valid transitions make one
    // encoder detent, greatly reducing bounce/false counts.
    const uint8_t clk = static_cast<uint8_t>(gpio_get_level(static_cast<gpio_num_t>(PIN_ENC_CLK)));
    const uint8_t dt = static_cast<uint8_t>(gpio_get_level(static_cast<gpio_num_t>(PIN_ENC_DT)));
    const uint8_t state = static_cast<uint8_t>((clk << 1) | dt);
    if (state != lastEncoderState) {
        const uint8_t transition = static_cast<uint8_t>((lastEncoderState << 2) | state);
        encoderAccumulator += encoderTransition(transition);
        lastEncoderState = state;

        if (encoderAccumulator >= 4) {
            encoderDelta += ENCODER_REVERSED ? -1 : 1;
            encoderAccumulator = 0;
        } else if (encoderAccumulator <= -4) {
            encoderDelta += ENCODER_REVERSED ? 1 : -1;
            encoderAccumulator = 0;
        }
    }
}

int8_t InputManager::encoderTransition(uint8_t transition) {
    // Gray-code quadrature lookup. Invalid transitions return 0.
    static const int8_t table[16] = {
        0, -1,  1,  0,
        1,  0,  0, -1,
       -1,  0,  0,  1,
        0,  1, -1,  0
    };
    return table[transition & 0x0F];
}

bool InputManager::isButtonPressed(ButtonID id) const {
    if (id < 0 || id >= NUM_BUTTONS) return false;
    return buttonStates[id];
}

bool InputManager::wasButtonClicked(ButtonID id) {
    if (id < 0 || id >= NUM_BUTTONS) return false;
    if (buttonEventFlags[id]) {
        buttonEventFlags[id] = false;
        return true;
    }
    return false;
}

bool InputManager::isEncoderButtonClicked() {
    return wasButtonClicked(ENCODER_SW);
}

int InputManager::getEncoderDelta() {
    const int delta = encoderDelta;
    encoderDelta = 0;
    return delta;
}
