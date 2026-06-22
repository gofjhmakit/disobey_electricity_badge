#pragma once

#include <Arduino.h>

void setupPins();
void updateButtonStates();
bool buttonPressedOnce(uint8_t pin);
bool consumeAnyButtonPressEvent();
bool anyButtonIsPressed();
