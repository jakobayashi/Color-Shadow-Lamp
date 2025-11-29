#ifndef STATE_H
#define STATE_H

#include <Arduino.h>
#include "LEDController.h"

enum class OperationMode {
    PARTY,
    MUSIC,
    WIFI,
    OFF,
};

class StateHandler {
private:
    OperationMode currentMode;
    LEDController &ledController;
    float partyHz = 0.6f; // cycles per second

public:
    StateHandler(LEDController &controller)
        : currentMode(OperationMode::WIFI), ledController(controller) {}

    void begin() {
        currentMode = OperationMode::WIFI;
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
        // Button handling disabled to avoid conflicts with remote control.
    }
};

#endif
