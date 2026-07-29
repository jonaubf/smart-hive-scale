#pragma once

#include <stdint.h>

// PCA9548A I2C multiplexer (I2C_MUX_ADDR, config.h) on the shared bus.
// The 4 NAU7802s (all fixed at 0x2A) each sit behind their own downstream
// channel (0..NUM_CORNERS-1); the DS3231 and the mux itself are upstream.
// Control is a single byte: bit N connects channel N.

// Probe the mux. Logs an error and returns false if it doesn't respond;
// safe to call the other functions afterwards (they fail cleanly).
bool i2cMuxBegin();
bool i2cMuxIsPresent();

// Connect exactly channel N (0-7), disconnecting the others.
bool i2cMuxSelect(uint8_t channel);

// Disconnect all downstream channels — call after finishing with a corner
// so a wedged NAU7802 can't hold the shared bus the DS3231 also lives on.
bool i2cMuxDeselectAll();
