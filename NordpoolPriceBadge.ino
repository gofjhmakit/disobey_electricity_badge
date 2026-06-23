// Nordpool Price Badge — Arduino entry point.
// All application logic lives in the module files alongside this sketch.

#include <Arduino.h>
#include "badge_app.h"

SET_LOOP_TASK_STACK_SIZE(16 * 1024);

void setup() {
  badgeAppSetup();
}

void loop() {
  badgeAppLoop();
}
