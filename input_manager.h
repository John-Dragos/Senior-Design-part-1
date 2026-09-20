#pragma once

#include "config.h"

enum ButtonID {
    ENCODER_SW = 0,
    BTN_1 = 1,
    BTN_2 = 2,
    BTN_3 = 3,
    NUM_BUTTONS = 4
};

class InputManager {
public:
    void begin();
    void update();

    bool isButtonPressed(ButtonID id) const;
    bool wasButtonClicked(ButtonID id);
    bool isEncoderButtonClicked();
    int getEncoderDelta();

private:
    const int buttonPins[NUM_BUTTONS] = {
        PIN_ENC_SW, PIN_BTN_1, PIN_BTN_2, PIN_BTN_3
    };

    bool buttonStates[NUM_BUTTONS] = {};
    bool lastButtonStates[NUM_BUTTONS] = {};
    bool buttonEventFlags[NUM_BUTTONS] = {};
    uint64_t lastDebounceTime[NUM_BUTTONS] = {};

    uint8_t lastEncoderState = 0;
    int8_t encoderAccumulator = 0;
    int encoderDelta = 0;

    static int8_t encoderTransition(uint8_t transition);
};
