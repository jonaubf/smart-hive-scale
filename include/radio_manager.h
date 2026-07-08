#pragma once

// Disable WiFi radio to reduce idle power draw (GSM mode).
void radioPowerDown();

// Re-enable WiFi stack after radioPowerDown() — required before AP/STA (e.g. config portal).
void radioPowerUpForPortal();
