#include "setup_button.h"

#include <Arduino.h>

#include "config.h"
#include "pins.h"

namespace {

constexpr unsigned long DEBOUNCE_MS = 50;
bool holdAnnounced = false;
bool portalRequested = false;
bool wasPressed = false;
unsigned long pressStartMs = 0;
unsigned long lastEdgeMs = 0;

}  // namespace

void setupButtonBegin() {
  pinMode(PIN_SETUP_BUTTON, INPUT_PULLUP);
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
