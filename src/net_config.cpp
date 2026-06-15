#include "net_config.h"
#include "config.h"
#include "mcp_server.h"
#include "mqtt_ha.h"
#include "watchdog.h"
#include "diag.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <ESPmDNS.h>

// Optional local WiFi credentials for direct bring-up (gitignored).
#if defined(__has_include)
#  if __has_include("secrets.h")
#    include "secrets.h"
#  endif
#endif

static Preferences prefs;
static String   staSsid;        // remembered for the reconnect watchdog
static String   staPsk;         // (WiFiManager runs with persistent creds off,
                                //  so WiFi.reconnect() alone can't recover)
static String   mqttHost;
static String   mqttPortStr = "1883";
static String   mqttUser;
static String   mqttPass;

String   netMqttHost() { return mqttHost; }
uint16_t netMqttPort() { return (uint16_t)mqttPortStr.toInt(); }
String   netMqttUser() { return mqttUser; }
String   netMqttPass() { return mqttPass; }

static void saveSettings();  // fwd

void netSetMqtt(const String& host, const String& port,
                const String& user, const String& pass) {
  mqttHost    = host;
  mqttPortStr = port.length() ? port : "1883";
  mqttUser    = user;
  if (pass.length()) mqttPass = pass;   // empty -> keep existing password
  saveSettings();
  Serial.printf("[net] MQTT settings updated: %s:%s\n",
                mqttHost.c_str(), mqttPortStr.c_str());
  mqttApplyConfig();                     // force reconnect with new settings
}

static void loadSettings() {
  prefs.begin("tdai", true);
  mqttHost    = prefs.getString("mqtt_host", "");
  mqttPortStr = prefs.getString("mqtt_port", "1883");
  mqttUser    = prefs.getString("mqtt_user", "");
  mqttPass    = prefs.getString("mqtt_pass", "");
  prefs.end();
}

static void saveSettings() {
  prefs.begin("tdai", false);
  prefs.putString("mqtt_host", mqttHost);
  prefs.putString("mqtt_port", mqttPortStr);
  prefs.putString("mqtt_user", mqttUser);
  prefs.putString("mqtt_pass", mqttPass);
  prefs.end();
}

// Run WiFiManager. onDemand=true forces the portal (button press);
// otherwise autoConnect() uses saved WiFi creds and only opens the
// portal if none work.
static void runPortal(bool onDemand) {
  WiFiManager wm;
  WiFiManagerParameter pHost("host", "MQTT host", mqttHost.c_str(), 64);
  WiFiManagerParameter pPort("port", "MQTT port", mqttPortStr.c_str(), 6);
  WiFiManagerParameter pUser("user", "MQTT user", mqttUser.c_str(), 64);
  WiFiManagerParameter pPass("pass", "MQTT password", mqttPass.c_str(), 64);
  wm.addParameter(&pHost);
  wm.addParameter(&pPort);
  wm.addParameter(&pUser);
  wm.addParameter(&pPass);

  wm.setConfigPortalTimeout(onDemand ? 300 : 0);  // on-demand auto-exits after 5 min

  // The portal blocks for minutes waiting on the user; don't let the watchdog
  // reboot mid-configuration. (No-op before the watchdog is armed in setup().)
  watchdogPause();

  bool ok;
  if (onDemand) {
    Serial.println("[net] starting on-demand config portal");
    ok = wm.startConfigPortal(WIFI_AP_NAME);
  } else {
    Serial.println("[net] autoConnect (portal only if no stored creds work)");
    ok = wm.autoConnect(WIFI_AP_NAME);
  }

  // Persist any MQTT params the user entered.
  mqttHost    = pHost.getValue();
  mqttPortStr = pPort.getValue();
  mqttUser    = pUser.getValue();
  mqttPass    = pPass.getValue();
  saveSettings();

  watchdogResume();

  if (!ok) {
    Serial.println("[net] not connected after portal; rebooting");
    diagMarkReboot(DIAG_RB_PORTAL);
    delay(1000);
    ESP.restart();
  }
}

// (Re)start the mDNS responder on the current STA interface. Safe to call
// again after a network change — must run for tdai2170.local to resolve.
static void startMdns() {
  MDNS.end();
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", MCP_HTTP_PORT);
    Serial.printf("[net] mDNS: http://%s.local/\n", MDNS_HOSTNAME);
  } else {
    Serial.println("[net] mDNS start failed");
  }
}

void netConfigBegin() {
  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);
  loadSettings();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HA_NODE_ID);

  bool buttonHeld = (digitalRead(CONFIG_BUTTON_PIN) == LOW);

#ifdef WIFI_SSID
  bool haveSecrets = (strlen(WIFI_SSID) > 0);
#else
  bool haveSecrets = false;
#endif

  bool connected = false;
  if (buttonHeld) {
    runPortal(true);                       // explicit: always open the portal
    connected = (WiFi.status() == WL_CONNECTED);
  } else if (haveSecrets) {
#ifdef WIFI_SSID
    // Bring-up fast path. On failure we DO NOT open the blocking portal, so the
    // device still reaches loop() and the USB serial console stays usable.
    Serial.printf("[net] secrets.h: connecting directly to '%s'\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(250);
    connected = (WiFi.status() == WL_CONNECTED);
    if (!connected)
      Serial.println("[net] secrets.h connect FAILED — running offline "
                     "(USB serial console still works for bench debugging)");
#endif
  } else {
    runPortal(false);                      // no secrets: captive portal
    connected = (WiFi.status() == WL_CONNECTED);
  }

  if (connected) {
    // Remember the live credentials so the watchdog can re-issue WiFi.begin();
    // WiFi.reconnect() can't recover them because persistent storage is off.
    staSsid = WiFi.SSID();
    staPsk  = WiFi.psk();
    WiFi.setAutoReconnect(true);
    startMdns();
    Serial.printf("[net] connected: %s  IP=%s\n",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[net] OFFLINE — no WiFi; web/MQTT/MCP unavailable, USB console active");
  }
}

void netConfigLoop() {
  // Detect a sustained BOOT-button hold to re-enter the config portal.
  static uint32_t pressStart = 0;
  if (digitalRead(CONFIG_BUTTON_PIN) == LOW) {
    if (pressStart == 0) pressStart = millis();
    else if (millis() - pressStart >= CONFIG_HOLD_MS) {
      Serial.println("[net] BOOT held -> entering config portal");
      mcpStop();          // free port 80 so the portal's web UI can bind it
      runPortal(true);
      mcpBegin();         // re-bind the MCP/web server after the portal closes
      if (WiFi.status() == WL_CONNECTED) startMdns();  // re-announce on new net
      pressStart = 0;
    }
  } else {
    pressStart = 0;
  }

  // Escalating reconnect watchdog. A plain WiFi.begin() every few seconds does
  // not recover a wedged ESP32 WiFi driver — which is exactly what happens when
  // the AP vanishes entirely (router reboot) rather than just weakening. So we
  // escalate: gentle retry -> full radio reset -> reboot.
  static uint32_t lastCheck    = 0;
  static bool     wasConnected = true;
  static uint32_t downSince    = 0;   // millis when the link dropped (0 = up)
  static uint8_t  attempts     = 0;

  if (millis() - lastCheck > 5000) {
    lastCheck = millis();
    // A router reboot often leaves the ESP32 re-associated at L2 (status reports
    // WL_CONNECTED) but with a failed DHCP renewal, so localIP() stays 0.0.0.0.
    // In that half-wedged state none of the tiers below ever fire and mDNS has no
    // address to announce — the device looks "online" forever. Treat "no valid
    // IP" as down so the escalation (and the >5 min reboot) actually recovers it.
    bool linkUp = (WiFi.status() == WL_CONNECTED);
    bool hasIp  = (WiFi.localIP() != IPAddress((uint32_t)0));
    bool up     = linkUp && hasIp;
    if (linkUp && !hasIp)
      Serial.println("[net] associated but no IP (DHCP not renewed) — treating as down");

    if (up) {
      if (!wasConnected) {            // link just came back
        Serial.printf("[net] reconnected  IP=%s\n", WiFi.localIP().toString().c_str());
        startMdns();                  // re-announce so tdai2170.local resolves
      }
      downSince = 0;
      attempts  = 0;
    } else if (staSsid.length()) {    // only escalate if we have known-good creds
      if (downSince == 0) downSince = millis();
      uint32_t downMs = millis() - downSince;
      attempts++;

      if (downMs > 300000UL) {                 // Tier 3: >5 min down -> reboot
        Serial.println("[net] WiFi down >5 min — rebooting");
        diagMarkReboot(DIAG_RB_WIFI_DOWN);
        delay(100);
        ESP.restart();
      } else if (attempts % 6 == 0) {          // Tier 2: ~every 30 s -> radio reset
        Serial.println("[net] WiFi still down — full radio reset");
        WiFi.disconnect(true, false);
        WiFi.mode(WIFI_OFF);
        delay(100);
        WiFi.mode(WIFI_STA);
        WiFi.setHostname(HA_NODE_ID);
        WiFi.setAutoReconnect(true);
        WiFi.begin(staSsid.c_str(), staPsk.c_str());
      } else {                                 // Tier 1: gentle reconnect
        Serial.printf("[net] WiFi down %us — reconnecting to %s\n",
                      (unsigned)(downMs / 1000), staSsid.c_str());
        WiFi.begin(staSsid.c_str(), staPsk.c_str());
      }
    } else {
      // No stored creds (never connected) — don't reboot-loop; just retry.
      Serial.println("[net] WiFi down (no stored creds) — reconnecting");
      WiFi.reconnect();
    }
    wasConnected = up;
  }
}
