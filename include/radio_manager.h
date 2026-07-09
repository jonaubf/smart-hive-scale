#pragma once

// Disable WiFi radio to reduce idle power draw (GSM mode).
void radioPowerDown();

// Full WiFi shutdown before deep sleep (chip reboots on wake). Safe to call
// esp_wifi_stop() here — unlike radioPowerDown(), which must leave the stack
// alive for a later config portal in the same session.
void radioDeepSleepPowerDown();

// Re-enable WiFi stack after radioPowerDown() — required before AP/STA (e.g. config portal).
void radioPowerUpForPortal();
