#include "battery_sensor.h"

#include <Arduino.h>

#include "config.h"
#include "pins.h"

namespace {

float clamp01(float value) {
  if (value < 0.0f) {
    return 0.0f;
  }
  if (value > 1.0f) {
    return 1.0f;
  }
  return value;
}

}  // namespace

void batterySensorBegin() {
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);
}

float batterySensorVoltage() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < BATTERY_ADC_SAMPLES; i++) {
    sum += analogRead(PIN_BATTERY_ADC);
    delay(2);
  }

  const float raw = static_cast<float>(sum) / BATTERY_ADC_SAMPLES;
  const float adcVoltage = (raw / BATTERY_ADC_MAX) * BATTERY_ADC_REF_V;
  return adcVoltage * BATTERY_DIVIDER_RATIO;
}

int batterySensorPercent() {
  const float voltage = batterySensorVoltage();
  const float normalized =
      (voltage - BATTERY_EMPTY_V) / (BATTERY_FULL_V - BATTERY_EMPTY_V);
  return static_cast<int>(clamp01(normalized) * 100.0f + 0.5f);
}
