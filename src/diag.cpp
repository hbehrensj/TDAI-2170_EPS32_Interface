#include "diag.h"
#include <esp_system.h>

// RTC "no-init" memory survives a software reset (and usually a brown-out) but
// is garbage after a clean power-on — exactly the discriminator we want.
static const uint32_t DIAG_MAGIC = 0xD1A6C0DE;

RTC_NOINIT_ATTR static uint32_t rtcMagic;
RTC_NOINIT_ATTR static uint32_t rtcCause;     // DiagReboot set before a restart
RTC_NOINIT_ATTR static uint32_t rtcUptimeMs;  // mirrored uptime (forensics)

static String lastResetText;

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

static const char* causeStr(uint32_t c) {
  switch (c) {
    case DIAG_RB_LOOP_WDT:   return "loop watchdog (stalled loop)";
    case DIAG_RB_WIFI_DOWN:  return "WiFi down >5 min";
    case DIAG_RB_SELFUPDATE: return "self-update";
    case DIAG_RB_PORTAL:     return "config portal";
    default:                 return nullptr;
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
    const char* c = causeStr(rtcCause);
    if (c) s += String(" — ") + c;
    if (rtcUptimeMs) s += " — ran " + fmtDuration(rtcUptimeMs) + " before reboot";
  }
  lastResetText = s;
  Serial.printf("[diag] last reset: %s\n", lastResetText.c_str());

  // Re-arm for next boot: mark RTC valid and clear the intentional-cause slot so
  // an *unexpected* reset next time shows up as just its hardware reason.
  rtcMagic    = DIAG_MAGIC;
  rtcCause    = DIAG_RB_NONE;
  rtcUptimeMs = 0;
}

void diagMarkReboot(DiagReboot cause) { rtcCause = cause; }
void diagTick() { rtcUptimeMs = millis(); }
String diagLastResetText() { return lastResetText; }
uint32_t diagUptimeSeconds() { return millis() / 1000; }
