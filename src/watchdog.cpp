#include "watchdog.h"
#include <Arduino.h>
#include <esp_timer.h>

// If loop() goes this long without a heartbeat we assume it is wedged and
// reboot. Generous enough to cover the slowest *normal* iteration (a blocking
// MQTT broker connect, a WiFi radio reset); anything legitimately longer pauses
// the watchdog explicitly.
static const uint32_t WD_TIMEOUT_MS  = 60000;
static const uint64_t WD_CHECK_US    = 5000000ULL;   // evaluate every 5 s

static esp_timer_handle_t wdTimer  = nullptr;
static volatile uint32_t  lastFeed = 0;
static volatile bool      paused   = false;

// Runs in the esp_timer task (priority 22), so it preempts and fires even when
// the loop task (priority 1) is stuck spinning or blocked.
static void onTick(void*) {
  if (paused || wdTimer == nullptr) return;
  if ((int32_t)(millis() - lastFeed) > (int32_t)WD_TIMEOUT_MS) {
    Serial.printf("\n[wdt] loop() stalled >%us — rebooting\n",
                  (unsigned)(WD_TIMEOUT_MS / 1000));
    Serial.flush();
    esp_restart();
  }
}

void watchdogBegin() {
  if (wdTimer) return;
  lastFeed = millis();
  esp_timer_create_args_t args = {};   // zero-init: dispatch defaults to TASK
  args.callback = &onTick;
  args.name     = "loopwdt";
  if (esp_timer_create(&args, &wdTimer) == ESP_OK) {
    esp_timer_start_periodic(wdTimer, WD_CHECK_US);
    Serial.printf("[wdt] loop watchdog armed (%us)\n",
                  (unsigned)(WD_TIMEOUT_MS / 1000));
  } else {
    Serial.println("[wdt] failed to create watchdog timer");
  }
}

void watchdogFeed()   { lastFeed = millis(); }
void watchdogPause()  { paused = true; }
void watchdogResume() { lastFeed = millis(); paused = false; }
