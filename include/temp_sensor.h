#pragma once

// DS18B20 on the scale frame (OneWire, external 4.7 kOhm pull-up).
// Returns NAN when the sensor is missing or the read fails.
bool tempSensorBegin();
float tempSensorReadC();
