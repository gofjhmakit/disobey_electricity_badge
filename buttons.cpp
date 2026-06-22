#include "buttons.h"
#include "app_state.h"
#include "config.h"

void setupPins() {
  pinMode(LED_ACTIVATE_PIN, OUTPUT);
  digitalWrite(LED_ACTIVATE_PIN, LOW);

  for (auto &button : g_app.buttons) {
    pinMode(button.pin, INPUT_PULLUP);
  }
}

void updateButtonStates() {
  unsigned long now = millis();
  for (auto &button : g_app.buttons) {
    bool reading = digitalRead(button.pin) == LOW;
    if (reading != button.last) {
      button.lastDebounce = now;
    }
    if ((now - button.lastDebounce) > 50) {
      button.currentState = reading;
    }
    button.last = reading;
  }
}

bool buttonPressedOnce(uint8_t pin) {
  for (auto &button : g_app.buttons) {
    if (button.pin == pin) {
      if (button.currentState && !button.lastReportedState) {
        button.lastReportedState = true;
        return true;
      }
      if (!button.currentState) {
        button.lastReportedState = false;
      }
      return false;
    }
  }
  return false;
}

bool consumeAnyButtonPressEvent() {
  for (auto &button : g_app.buttons) {
    if (button.currentState && !button.lastReportedState) {
      button.lastReportedState = true;
      return true;
    }
  }
  return false;
}

bool anyButtonIsPressed() {
  for (auto &button : g_app.buttons) {
    if (button.currentState) {
      return true;
    }
  }
  return false;
}
