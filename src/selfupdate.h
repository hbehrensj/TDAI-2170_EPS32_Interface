#pragma once
#include <Arduino.h>

// Self-update from GitHub releases. The device fetches a small version.json
// from the "latest" release, compares it to its compiled FIRMWARE_VERSION_STR,
// and pulls firmware.bin via HTTPS when a newer version is published.

void   selfUpdateLoop();       // auto-check shortly after boot, then daily
String selfUpdateCheckNow();   // manual trigger (web UI); returns a status line
