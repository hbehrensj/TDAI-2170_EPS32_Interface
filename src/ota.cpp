#include "ota.h"
#include "config.h"
#include <WiFi.h>
#include <ArduinoOTA.h>

static bool started = false;

void otaBegin() {
  if (started) return;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[ota] WiFi not connected; OTA not started");
    return;
  }

  ArduinoOTA.setHostname(MDNS_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    Serial.println("[ota] update starting");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\n[ota] update complete, rebooting");
  });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
    Serial.printf("[ota] %u%%\r", (t ? p * 100 / t : 0));
  });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("[ota] error %u\n", e);
  });

  ArduinoOTA.begin();
  started = true;
  Serial.printf("[ota] ready: %s.local (espota)\n", MDNS_HOSTNAME);
}

void otaLoop() {
  if (!started) {
    if (WiFi.status() == WL_CONNECTED) otaBegin();  // start once WiFi is up
    return;
  }
  ArduinoOTA.handle();
}
