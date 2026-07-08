#pragma once

#include <stdint.h>

void setupButtonBegin();
// Returns true once when the button has been held for the configured duration.
bool setupButtonPortalRequested();
// Short press or serial byte during blocking publish/network work.
bool setupButtonBreakRequested();
bool setupButtonBreakWasRequested();
void setupButtonClearBreak();
