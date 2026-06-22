#include "power_save.h"
#include "app_state.h"
#include "config.h"
#include "display.h"
#include "led_controller.h"
#include "time_utils.h"

void enterPowerSaveMode() {
  if (g_app.powerSaveActive) {
    return;
  }
  g_app.powerSaveActive = true;
  digitalWrite(TFT_BL, LOW);
  refreshPowerSaveLEDs();

  time_t now = time(nullptr);
  if (now != 0) {
    tm nowTm;
    localtime_r(&now, &nowTm);
    g_app.lastPowerSaveLedUpdateSlot = getLocalHourSlot(nowTm);
  }
}

void exitPowerSaveMode() {
  if (!g_app.powerSaveActive) {
    return;
  }
  g_app.powerSaveActive = false;
  digitalWrite(TFT_BL, HIGH);
  if (g_app.showSettings) {
    drawSettingsScreen();
  } else {
    drawCurrentScreen();
  }
}

void registerInteraction() {
  g_app.lastInteractionMs = millis();
  if (g_app.powerSaveActive) {
    exitPowerSaveMode();
  }
}

void updatePowerSaveLEDsOnHour() {
  if (!g_app.powerSaveActive) {
    return;
  }

  time_t now = time(nullptr);
  if (now == 0) {
    return;
  }

  tm nowTm;
  localtime_r(&now, &nowTm);
  if (nowTm.tm_min != 0) {
    return;
  }

  int slot = getLocalHourSlot(nowTm);
  if (slot == g_app.lastPowerSaveLedUpdateSlot) {
    return;
  }

  refreshPowerSaveLEDs();
  g_app.lastPowerSaveLedUpdateSlot = slot;
}
