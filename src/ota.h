#pragma once
#include <Arduino.h>

// Arduino OTA (over-the-air) firmware updates over WiFi.
// After otaBegin(), the device is flashable from PlatformIO with:
//   pio run -e ota -t upload
// (or any espota client) using OTA_PASSWORD. Avoids native-USB flashing.

void otaBegin();   // no-op if WiFi is not connected
void otaLoop();
