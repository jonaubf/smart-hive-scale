#pragma once

// forceAp: start (or switch to) the standalone AP portal even if the
// STA portal is already running — used for the button hold / `portal` command
// so a new WiFi network can be configured.
void maintenancePortalBegin(bool forceAp = false);
void maintenancePortalLoop();
bool maintenancePortalIsActive();
bool maintenancePortalIsStaMode();
