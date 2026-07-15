#include "temp_sensor.h"

#include <Arduino.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <math.h>

#include "config.h"
#include "pins.h"

namespace {

OneWire oneWire(PIN_TEMP_ONEWIRE);
DallasTemperature sensors(&oneWire);
DeviceAddress sensorAddress;
bool sensorPresent = false;

}  // namespace

bool tempSensorBegin() {
  sensors.begin();
  sensorPresent = sensors.getAddress(sensorAddress, 0);
  if (!sensorPresent) {
    Serial.println(F("ERR DS18B20 not found on OneWire — check wiring/pull-up"));
    return false;
  }
  sensors.setResolution(sensorAddress, TEMP_SENSOR_RESOLUTION_BITS);
  return true;
}

float tempSensorReadC() {
  if (!sensorPresent) {
    return NAN;
  }
  // Blocking conversion: up to 750 ms at 12 bits.
  sensors.requestTemperaturesByAddress(sensorAddress);
  const float tempC = sensors.getTempC(sensorAddress);
  if (tempC == DEVICE_DISCONNECTED_C) {
    return NAN;
  }
  return tempC;
}
