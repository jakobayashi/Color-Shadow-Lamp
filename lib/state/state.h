#ifndef STATE_H
#define STATE_H

#include <Arduino.h>
#include "LEDController.h"

enum class OperationMode {
    RGB,
    LTT,
    PARTY,
    WIFI,
    OFF,
};

class StateHandler {
private:
    static constexpr int BUTTON_PIN = 9;
    static constexpr int DEBOUNCE_TIME = 50;
    
    OperationMode currentMode;
    unsigned long lastButtonPress;
    bool buttonWasPressed;
    LEDController &ledController;
    float partyHz = 0.6f; // cycles per second

public:
    StateHandler(LEDController &controller)
        : currentMode(OperationMode::RGB), lastButtonPress(0), buttonWasPressed(false), ledController(controller) {}

    void begin() {
        currentMode = OperationMode::RGB;
        pinMode(BUTTON_PIN, INPUT);
    }

    OperationMode getCurrentMode() const {
        return currentMode;
    }

    void setMode(OperationMode mode) {
        currentMode = mode;
    }

    void setPartyHz(float hz) {
        partyHz = constrain(hz, 0.05f, 5.0f); // clamp to sensible range
    }

    float getPartyHz() const {
        return partyHz;
    }

    void update() {
        bool buttonIsPressed = (digitalRead(BUTTON_PIN) == 0);
        
        if (buttonIsPressed && !buttonWasPressed) {
            unsigned long now = millis();
            if (now - lastButtonPress >= DEBOUNCE_TIME) {
                lastButtonPress = now;
                
                switch (currentMode) {
                    case OperationMode::OFF:
                        currentMode = OperationMode::RGB;
                        break;
                    case OperationMode::RGB:
                        currentMode = OperationMode::LTT;
                        break;
                    case OperationMode::LTT:
                        currentMode = OperationMode::PARTY;
                        break;
                    case OperationMode::PARTY:
                        currentMode = OperationMode::WIFI;
                        break;
                    case OperationMode::WIFI:
                        currentMode = OperationMode::OFF;
                        break;
                }
            }
        }
        buttonWasPressed = buttonIsPressed;
    }
};

#endif
