#include "selfupdate.h"
#include "config.h"
#include "watchdog.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>

// Parse "a.b.c" into a comparable number. Tolerates missing parts.
static uint32_t verNum(const String& v) {
  int a = 0, b = 0, c = 0;
  sscanf(v.c_str(), "%d.%d.%d", &a, &b, &c);
  return (uint32_t)a * 1000000 + (uint32_t)b * 1000 + (uint32_t)c;
}

// Fetch the manifest; on a newer version, perform the update (which reboots on
// success and does not return). Returns a human-readable status string.
static String doUpdate() {
  if (WiFi.status() != WL_CONNECTED) return "offline";

  // --- Fetch + parse the manifest, then FREE its TLS client. A WiFiClientSecure
  //     holds a large TLS buffer; keeping it alive while the firmware download
  //     opens a second TLS context exhausts the heap (-> connection refused). ---
  String remote;
  {
    WiFiClientSecure client;
    client.setInsecure();               // no cert pinning (GitHub TLS)
    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setConnectTimeout(8000);
    if (!http.begin(client, UPDATE_VERSION_URL)) return "manifest begin failed";

    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return String("manifest HTTP ") + code; }
    String body = http.getString();
    http.end();

    JsonDocument d;
    if (deserializeJson(d, body)) return "bad manifest JSON";
    remote = d["version"] | "";
  }
  if (remote.isEmpty()) return "no version in manifest";

  Serial.printf("[update] local=%s remote=%s\n", FIRMWARE_VERSION_STR, remote.c_str());
  if (verNum(remote) <= verNum(FIRMWARE_VERSION_STR))
    return String("up to date (") + FIRMWARE_VERSION_STR + ")";

  Serial.printf("[update] newer release %s -> downloading firmware (heap=%u)\n",
                remote.c_str(), ESP.getFreeHeap());
  WiFiClientSecure uclient;
  uclient.setInsecure();
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  httpUpdate.rebootOnUpdate(true);      // reboots into new image on success

  // The download+flash blocks loop() for far longer than the watchdog window;
  // pause it so a slow link isn't mistaken for a stall. On success update()
  // reboots and never returns; on failure we resume below.
  watchdogPause();
  t_httpUpdate_return ret = httpUpdate.update(uclient, UPDATE_FIRMWARE_URL);
  watchdogResume();
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      return String("update failed: ") + httpUpdate.getLastErrorString();
    case HTTP_UPDATE_NO_UPDATES:
      return "no update (server)";
    default:
      return "updating (rebooting)";    // normally unreachable (reboots first)
  }
}

String selfUpdateCheckNow() {
  String s = doUpdate();
  Serial.printf("[update] %s\n", s.c_str());
  return s;
}

void selfUpdateLoop() {
  static uint32_t last = 0;
  static bool firstDone = false;
  uint32_t now = millis();

  if (!firstDone) {
    if (now < 30000) return;            // give WiFi/services time to settle
    firstDone = true;
    last = now;
    selfUpdateCheckNow();
  } else if (now - last >= UPDATE_CHECK_INTERVAL_MS) {
    last = now;
    selfUpdateCheckNow();
  }
}
