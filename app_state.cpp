#include "app_state.h"

AppState g_app;

void initAppButtons() {
  const uint8_t pins[] = {
    BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_STICK,
    BTN_A, BTN_B, BTN_START, BTN_SELECT
  };
  for (int i = 0; i < 10; ++i) {
    g_app.buttons[i] = {pins[i], false, false, false, 0};
  }
}
