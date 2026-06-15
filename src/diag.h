#pragma once
#include <Arduino.h>

// Reboot forensics. Captures *why* the device last restarted and exposes it
// remotely (web UI + HA sensor), so a reboot that happens while nobody is
// watching the serial console can still be diagnosed after the device recovers.
//
// It combines two sources:
//   - esp_reset_reason(): the hardware/SW reset cause (brown-out, panic,
//     interrupt/task watchdog, software restart, power-on, …)
//   - an RTC marker we set just before our own intentional ESP.restart() calls,
//     so a software restart can be attributed to the right cause.

enum DiagReboot : uint32_t {
  DIAG_RB_NONE = 0,    // no intentional restart recorded (crash / external / power)
  DIAG_RB_LOOP_WDT,    // our software loop watchdog fired
  DIAG_RB_WIFI_DOWN,   // WiFi down >5 min
  DIAG_RB_SELFUPDATE,  // firmware self-update
  DIAG_RB_PORTAL,      // config portal exited without a connection
};

void   diagBegin();                       // call first in setup(): latch last-boot info
void   diagMarkReboot(DiagReboot cause);  // call right before an intentional restart
void   diagTick();                        // cheap: mirror uptime for crash forensics
String diagLastResetText();               // human-readable summary of the previous boot
uint32_t diagUptimeSeconds();             // current uptime, seconds
