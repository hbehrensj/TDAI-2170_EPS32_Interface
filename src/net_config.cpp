#include "net_config.h"
#include "config.h"
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
static String   mqttHost;
static String   mqttPortStr = "1883";
static String   mqttUser;
static String   mqttPass;

String   netMqttHost() { return mqttHost; }
uint16_t netMqttPort() { return (uint16_t)mqttPortStr.toInt(); }
String   netMqttUser() { return mqttUser; }
String   netMqttPass() { return mqttPass; }

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

  if (!ok) {
    Serial.println("[net] not connected after portal; rebooting");
    delay(1000);
    ESP.restart();
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
    if (MDNS.begin(MDNS_HOSTNAME)) {
      MDNS.addService("http", "tcp", MCP_HTTP_PORT);
      Serial.printf("[net] mDNS: http://%s.local/\n", MDNS_HOSTNAME);
    }
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
      runPortal(true);
      pressStart = 0;
    }
  } else {
    pressStart = 0;
  }

  // Lightweight reconnect watchdog.
  static uint32_t lastCheck = 0;
  if (millis() - lastCheck > 5000) {
    lastCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[net] WiFi dropped, reconnecting");
      WiFi.reconnect();
    }
  }
}
