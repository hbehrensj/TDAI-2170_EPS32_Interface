#include "diag.h"
#include <esp_system.h>

// RTC "no-init" memory survives a software reset (and usually a brown-out) but
// is garbage after a clean power-on — exactly the discriminator we want.
static const uint32_t DIAG_MAGIC = 0xD1A6C0DE;

RTC_NOINIT_ATTR static uint32_t rtcMagic;
RTC_NOINIT_ATTR static uint32_t rtcCause;     // DiagReboot set before a restart
RTC_NOINIT_ATTR static uint32_t rtcUptimeMs;  // mirrored uptime (forensics)
RTC_NOINIT_ATTR static uint32_t rtcStallPhase;// LoopPhase active when loop stalled

static volatile uint8_t curPhase = PH_NONE;   // current loop() step (RAM)
static String lastResetText;

static int      lastDropRssi  = 0;
static uint32_t lastDownMs    = 0;
static uint32_t dropCount     = 0;
static bool     haveDropData  = false;

static const char* resetReasonStr(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "external reset";
    case ESP_RST_SW:        return "software restart";
    case ESP_RST_PANIC:     return "crash (panic)";
    case ESP_RST_INT_WDT:   return "interrupt watchdog";
    case ESP_RST_TASK_WDT:  return "task watchdog";
    case ESP_RST_WDT:       return "watchdog";
    case ESP_RST_BROWNOUT:  return "brown-out (power dip)";
    case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
    default:                return "unknown";
  }
}

static const char* phaseStr(uint32_t p) {
  switch (p) {
    case PH_NET:      return "wifi/reconnect";
    case PH_OTA:      return "ota";
    case PH_UPDATE:   return "self-update";
    case PH_LYNGDORF: return "uart";
    case PH_TCP:      return "tcp bridge";
    case PH_MQTT:     return "mqtt";
    case PH_MCP:      return "web/mcp";
    default:          return nullptr;
  }
}

static String causeStr(uint32_t c, uint32_t phase) {
  switch (c) {
    case DIAG_RB_LOOP_WDT: {
      const char* ph = phaseStr(phase);
      return ph ? String("loop watchdog (stalled in: ") + ph + ")"
                : String("loop watchdog (stalled loop)");
    }
    case DIAG_RB_WIFI_DOWN:  return "WiFi down >5 min";
    case DIAG_RB_SELFUPDATE: return "self-update";
    case DIAG_RB_PORTAL:     return "config portal";
    default:                 return String();
  }
}

static String fmtDuration(uint32_t ms) {
  uint32_t s = ms / 1000, m = s / 60, h = m / 60;
  char buf[32];
  if (h)      snprintf(buf, sizeof buf, "%uh%02um", (unsigned)h, (unsigned)(m % 60));
  else if (m) snprintf(buf, sizeof buf, "%um%02us", (unsigned)m, (unsigned)(s % 60));
  else        snprintf(buf, sizeof buf, "%us", (unsigned)s);
  return String(buf);
}

void diagBegin() {
  esp_reset_reason_t rr = esp_reset_reason();
  bool valid = (rtcMagic == DIAG_MAGIC);   // false after a clean power-on
  String s = resetReasonStr(rr);

  if (valid) {
    String c = causeStr(rtcCause, rtcStallPhase);
    if (c.length()) s += String(" — ") + c;
    if (rtcUptimeMs) s += " — ran " + fmtDuration(rtcUptimeMs) + " before reboot";
  }
  lastResetText = s;
  Serial.printf("[diag] last reset: %s\n", lastResetText.c_str());

  // Re-arm for next boot: mark RTC valid and clear the intentional-cause slot so
  // an *unexpected* reset next time shows up as just its hardware reason.
  rtcMagic      = DIAG_MAGIC;
  rtcCause      = DIAG_RB_NONE;
  rtcUptimeMs   = 0;
  rtcStallPhase = PH_NONE;
}

void diagMarkReboot(DiagReboot cause) { rtcCause = cause; }
void diagMarkLoopStall() { rtcCause = DIAG_RB_LOOP_WDT; rtcStallPhase = curPhase; }
void diagSetPhase(LoopPhase p) { curPhase = (uint8_t)p; }
void diagTick() { rtcUptimeMs = millis(); }
String diagLastResetText() { return lastResetText; }
uint32_t diagUptimeSeconds() { return millis() / 1000; }

void diagMarkWifiDown(int rssiAtDrop) {
  lastDropRssi = rssiAtDrop;
  dropCount++;
  haveDropData = true;
}

void diagMarkWifiUp(uint32_t downMs) { lastDownMs = downMs; }

String diagLastWifiDropText() {
  if (!haveDropData) return "none yet";
  return "RSSI " + String(lastDropRssi) + "dBm before drop — down " +
         fmtDuration(lastDownMs) + " (" + String(dropCount) +
         (dropCount == 1 ? " drop" : " drops") + " since boot)";
}
