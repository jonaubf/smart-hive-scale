#include "weight_sensor.h"

#include <HX711.h>
#include <driver/gpio.h>

#include "config.h"
#include "pins.h"

namespace {

constexpr uint8_t HX711_GAIN = 128;
constexpr unsigned long HX711_READY_TIMEOUT_MS = 1500;
constexpr uint8_t HX711_MEDIAN_MAX_SAMPLES = 31;

HX711 scale;

// SCK high >60 µs powers the HX711 down; low powers it up. Output settles
// ~400 ms after power-up.
void powerCycle() {
  scale.power_down();
  delayMicroseconds(100);
  scale.power_up();
  delay(400);
}

bool readSingle(long &rawOut) {
  if (!scale.wait_ready_timeout(HX711_READY_TIMEOUT_MS)) {
    // A wedged HX711 (noise glitch on SCK mid-conversion) recovers after a
    // power cycle; give it one chance before reporting failure.
    powerCycle();
    if (!scale.wait_ready_timeout(HX711_READY_TIMEOUT_MS)) {
      return false;
    }
  }
  rawOut = scale.read();
  return true;
}

long medianSorted(long *values, uint8_t count) {
  for (uint8_t i = 1; i < count; i++) {
    const long key = values[i];
    int j = static_cast<int>(i) - 1;
    while (j >= 0 && values[j] > key) {
      values[j + 1] = values[j];
      j--;
    }
    values[j + 1] = key;
  }
  return values[count / 2];
}

}  // namespace

bool weightSensorBegin() {
  // Release the deep-sleep hold on SCK (set in weightSensorPowerDown) so the
  // library can drive it low and power the HX711 back up.
  gpio_hold_dis(static_cast<gpio_num_t>(PIN_HX711_SCK));

  scale.begin(PIN_HX711_DT, PIN_HX711_SCK);
  // Pull-up keeps DOUT from floating between conversions and during modem/
  // WiFi bursts — floating DOUT reads as noise.
  pinMode(PIN_HX711_DT, INPUT_PULLUP);
  scale.set_gain(HX711_GAIN);

  // HX711 output only settles ~400 ms after power-up (it is power-cycled by
  // deep sleep); the first conversions are off and would corrupt averages.
  long discard = 0;
  for (uint8_t i = 0; i < HX711_WARMUP_READS; i++) {
    if (!readSingle(discard)) {
      break;
    }
  }
  return true;
}

WeightSensorReading weightSensorReadRaw(uint8_t samples) {
  WeightSensorReading result{false, 0};

  if (samples == 0) {
    samples = 1;
  }

  if (!readSingle(result.raw)) {
    return result;
  }

  if (samples == 1) {
    result.ok = true;
    return result;
  }

  result.raw = scale.read_average(samples);
  result.ok = true;
  return result;
}

void weightSensorPowerDown() {
  scale.power_down();
  // Deep sleep resets GPIOs to inputs; without a hold SCK would float and the
  // HX711 could wake back up mid-sleep. GPIO32 is an RTC pad.
  gpio_hold_en(static_cast<gpio_num_t>(PIN_HX711_SCK));
  gpio_deep_sleep_hold_en();
}

WeightSensorReading weightSensorReadRawMedian(uint8_t samples, uint8_t warmupReads) {
  WeightSensorReading result{false, 0};

  if (samples == 0 || samples > HX711_MEDIAN_MAX_SAMPLES) {
    return result;
  }

  long value = 0;
  for (uint8_t i = 0; i < warmupReads; i++) {
    if (!readSingle(value)) {
      return result;
    }
  }

  long values[HX711_MEDIAN_MAX_SAMPLES];
  for (uint8_t i = 0; i < samples; i++) {
    if (!readSingle(values[i])) {
      return result;
    }
  }

  result.raw = medianSorted(values, samples);
  result.ok = true;
  return result;
}
