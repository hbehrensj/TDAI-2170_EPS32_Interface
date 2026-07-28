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

// Which loop() step is currently executing. Recorded continuously so that when
// the loop watchdog fires we can report *where* loop() was stuck.
enum LoopPhase : uint8_t {
  PH_NONE = 0, PH_NET, PH_OTA, PH_UPDATE, PH_LYNGDORF, PH_TCP, PH_MQTT, PH_MCP,
};

void   diagBegin();                       // call first in setup(): latch last-boot info
void   diagMarkReboot(DiagReboot cause);  // call right before an intentional restart
void   diagMarkLoopStall();               // call from the watchdog at stall time
void   diagSetPhase(LoopPhase p);         // mark the current loop() step (cheap)
void   diagTick();                        // cheap: mirror uptime for crash forensics
String diagLastResetText();               // human-readable summary of the previous boot
uint32_t diagUptimeSeconds();             // current uptime, seconds

// WiFi-drop forensics (no reboot involved — an AP can deauth a client, e.g. an
// AiMesh "Roaming Assistant" kicking weak-RSSI clients, without the device ever
// crashing or restarting). Tracked in RAM only: these outages don't survive a
// reboot anyway, and none of what we've seen has needed one.
void   diagMarkWifiDown(int rssiAtDrop);  // call once when a real disconnect is confirmed
void   diagMarkWifiUp(uint32_t downMs);   // call once when recovery is confirmed
String diagLastWifiDropText();            // human-readable summary of the most recent outage
