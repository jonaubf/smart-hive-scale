#include "setup_button.h"

#include <Arduino.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "config.h"
#include "pins.h"

namespace {

constexpr unsigned long DEBOUNCE_MS = 50;
bool holdAnnounced = false;
bool portalRequested = false;
bool breakRequested = false;
bool wasPressed = false;
unsigned long pressStartMs = 0;
unsigned long lastEdgeMs = 0;

}  // namespace

void setupButtonBegin() {
  // Release RTC mux state left over from ext0 deep-sleep wakeup config.
  rtc_gpio_deinit(static_cast<gpio_num_t>(PIN_SETUP_BUTTON));
  pinMode(PIN_SETUP_BUTTON, INPUT_PULLUP);
}

void setupButtonPrepareDeepSleepWakeup() {
  const gpio_num_t buttonPin = static_cast<gpio_num_t>(PIN_SETUP_BUTTON);
  rtc_gpio_deinit(buttonPin);
  rtc_gpio_pullup_en(buttonPin);
  rtc_gpio_pulldown_dis(buttonPin);
  esp_sleep_enable_ext0_wakeup(buttonPin, 0);
}

// One-shot: returns true exactly once per 10s hold; re-arms on button release.
bool setupButtonPortalRequested() {
  const bool pressed = digitalRead(PIN_SETUP_BUTTON) == LOW;
  const unsigned long now = millis();

  if (pressed != wasPressed && now - lastEdgeMs >= DEBOUNCE_MS) {
    wasPressed = pressed;
    lastEdgeMs = now;
    if (pressed) {
      pressStartMs = now;
      holdAnnounced = false;
      portalRequested = false;
      Serial.println(F("Setup button pressed — hold 10s for config portal"));
    }
  }

  if (!pressed) {
    return false;
  }

  const unsigned long heldMs = now - pressStartMs;
  if (!holdAnnounced && heldMs >= SETUP_BUTTON_HOLD_MS / 2) {
    holdAnnounced = true;
    Serial.println(F("Setup button — keep holding..."));
  }

  if (heldMs >= SETUP_BUTTON_HOLD_MS && !portalRequested) {
    portalRequested = true;
    Serial.println(F("Setup button held 10s — opening config portal"));
    return true;
  }

  return false;
}

bool setupButtonBreakRequested() {
  if (digitalRead(PIN_SETUP_BUTTON) == LOW) {
    breakRequested = true;
    return true;
  }
  if (Serial.available() > 0) {
    while (Serial.available() > 0) {
      Serial.read();
    }
    breakRequested = true;
    return true;
  }
  return false;
}

bool setupButtonBreakWasRequested() { return breakRequested; }

void setupButtonClearBreak() { breakRequested = false; }
