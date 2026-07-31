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
#include "ping/ping_sock.h"
#include <freertos/semphr.h>

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

// Actively probe whether the network is *actually* reachable, not just
// whether the radio thinks it's associated. WiFi.status()==WL_CONNECTED plus
// a valid IP only means the STA associated and got a DHCP lease at some
// point — it says nothing about whether the AP-side entry is still alive.
// Seen in the field: the device sits fully "connected" by that definition,
// completely unreachable from the rest of the network, for hours, with none
// of the escalation tiers below ever firing because they never see "down".
//
// Ping the DHCP-assigned gateway rather than any particular service (e.g.
// the MQTT broker): a service can have its own downtime independent of
// whether this device's WiFi is actually fine, which would falsely blame
// WiFi for an outage on the other end. The gateway is the right thing to
// trust for "is my network connectivity working".
static bool gatewayReachable() {
  IPAddress gw = WiFi.gatewayIP();
  if (gw == IPAddress((uint32_t)0)) return true;   // no gateway known yet

  static SemaphoreHandle_t pingDone = xSemaphoreCreateBinary();
  static volatile bool     gotReply;
  gotReply = false;

  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.target_addr = IPADDR4_INIT_BYTES(gw[0], gw[1], gw[2], gw[3]);
  cfg.count       = 1;
  cfg.timeout_ms  = 2000;

  esp_ping_callbacks_t cbs = {};
  cbs.on_ping_success = [](esp_ping_handle_t, void*) { gotReply = true; };
  cbs.on_ping_end     = [](esp_ping_handle_t, void*) { xSemaphoreGive(pingDone); };

  esp_ping_handle_t hdl;
  if (esp_ping_new_session(&cfg, &cbs, &hdl) != ESP_OK) return true;  // local error; don't false-flag
  esp_ping_start(hdl);
  xSemaphoreTake(pingDone, pdMS_TO_TICKS(3000));   // session itself finishes within ~timeout_ms
  esp_ping_delete_session(hdl);
  return gotReply;
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
    // Production path. Connect to the network WiFiManager has stored, but NEVER
    // fall into the config portal on failure: a temporary router/AP outage must
    // not turn the device into an unsolicited hotspot that needs manual WiFi
    // reconfiguration. Instead stay offline and let the reconnect watchdog (and
    // its 5-min reboot) keep retrying until the known network returns. The setup
    // portal stays reachable on demand by holding the BOOT button.
    WiFiManager wm;
    if (wm.getWiFiIsSaved() && wm.getWiFiSSID().length()) {
      staSsid = wm.getWiFiSSID();           // seed the watchdog up front so it
      staPsk  = wm.getWiFiPass();           // retries these creds even if this
                                            // boot's attempt fails
      Serial.printf("[net] connecting to stored network '%s'\n", staSsid.c_str());
      WiFi.begin(staSsid.c_str(), staPsk.c_str());
      uint32_t t0 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(250);
      connected = (WiFi.status() == WL_CONNECTED);
      if (!connected)
        Serial.println("[net] stored network unavailable — staying offline and "
                       "retrying (hold BOOT for the setup portal)");
    } else {
      runPortal(false);                     // genuinely unconfigured: first-time setup
      connected = (WiFi.status() == WL_CONNECTED);
    }
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
  static uint32_t lastCheck       = 0;
  static bool     wasConnected    = true;
  static uint32_t downSince       = 0;   // millis when the link dropped (0 = up)
  static uint8_t  attempts        = 0;
  static int      lastKnownRssi   = 0;   // RSSI as of the most recent up poll (drop forensics)
  static uint32_t lastReachCheck  = 0;
  static uint8_t  reachFails      = 0;   // consecutive failed reachability probes

  // A couple of consecutive failed pings (~60-90s of confirmed
  // unreachability, since each probe itself can take up to 3s and they're
  // spaced 30s apart) before treating WiFi.status() as a lie — one dropped
  // ping alone could just be a transient blip.
  static const uint8_t REACH_FAILS_THRESHOLD = 2;

  // v1.5.7 tried debouncing "up" behind several consecutive polls before
  // trusting a reconnect, to stop a flapping AP-side deauth (e.g. AiMesh
  // "Roaming Assistant") from perpetually resetting this clock. In the field
  // that backfired badly: while flapping, attempts/downSince no longer reset
  // on the brief successes that *do* happen, so Tier 2 (a full radio
  // off/on cycle) started firing roughly every 30s of not-fully-stable time
  // instead of only after sustained downtime — and each reset is itself
  // disruptive (brief total unreachability while it re-associates), so it
  // compounded a marginal link into a much worse one. Reverted to resetting
  // on every single successful poll, same as before v1.5.7: don't make a
  // shaky connection worse by hitting it with a disruptive "fix" every 30s.
  if (millis() - lastCheck > 5000) {
    lastCheck = millis();
    // A router reboot often leaves the ESP32 re-associated at L2 (status reports
    // WL_CONNECTED) but with a failed DHCP renewal, so localIP() stays 0.0.0.0.
    // In that half-wedged state none of the tiers below ever fire and mDNS has no
    // address to announce — the device looks "online" forever. Treat "no valid
    // IP" as down so the escalation (and the >5 min reboot) actually recovers it.
    bool linkUp = (WiFi.status() == WL_CONNECTED);
    bool hasIp  = (WiFi.localIP() != IPAddress((uint32_t)0));
    if (linkUp && !hasIp)
      Serial.println("[net] associated but no IP (DHCP not renewed) — treating as down");

    // Only spend a probe while the radio otherwise looks healthy — if it's
    // already down by the cheap checks there's no need to also wait out a
    // TCP connect timeout.
    if (linkUp && hasIp && millis() - lastReachCheck > 30000) {
      lastReachCheck = millis();
      if (gatewayReachable()) {
        reachFails = 0;
      } else {
        reachFails++;
        Serial.printf("[net] gateway ping failed (%u/%u) despite WiFi status looking connected\n",
                      (unsigned)reachFails, (unsigned)REACH_FAILS_THRESHOLD);
      }
    }
    bool zombie = (reachFails >= REACH_FAILS_THRESHOLD);
    bool up     = linkUp && hasIp && !zombie;
    if (zombie && linkUp && hasIp)
      Serial.println("[net] WiFi status says connected but gateway is unpingable — treating as down");

    if (up) {
      lastKnownRssi = WiFi.RSSI();   // last-seen-good RSSI, for forensics if it drops next
      if (!wasConnected) {            // link just came back
        Serial.printf("[net] reconnected  IP=%s\n", WiFi.localIP().toString().c_str());
        startMdns();                  // re-announce so tdai2170.local resolves
        diagMarkWifiUp(millis() - downSince);
      }
      downSince    = 0;
      attempts     = 0;
      wasConnected = true;
    } else if (staSsid.length()) {    // only escalate if we have known-good creds
      wasConnected = false;
      if (downSince == 0) {
        downSince = millis();
        diagMarkWifiDown(lastKnownRssi);   // RSSI just before it dropped
      }
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
      wasConnected = false;
      Serial.println("[net] WiFi down (no stored creds) — reconnecting");
      WiFi.reconnect();
    }
  }
}
