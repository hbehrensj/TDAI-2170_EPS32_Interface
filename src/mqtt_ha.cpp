#include "mqtt_ha.h"
#include "config.h"
#include "net_config.h"
#include "lyngdorf.h"
#include "diag.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static WiFiClient   net;
static PubSubClient mqtt(net);

// Topic helpers ------------------------------------------------------------
static const String base = String(HA_NODE_ID);
static String tState(const char* obj) { return base + "/" + obj + "/state"; }
static String tCmd(const char* obj)   { return base + "/" + obj + "/set"; }
static String availTopic()            { return base + "/availability"; }

// Build the HA "device" object shared by all entities.
static void addDevice(JsonObject dev) {
  JsonArray ids = dev["identifiers"].to<JsonArray>();
  ids.add(HA_NODE_ID);
  dev["name"] = HA_DEVICE_NAME;
  dev["manufacturer"] = "Lyngdorf";
  dev["model"] = "TDAI-2170";
}

static void publishConfig(const char* component, const char* obj,
                          JsonDocument& doc) {
  doc["availability_topic"] = availTopic();
  doc["unique_id"] = base + "_" + obj;
  addDevice(doc["device"].to<JsonObject>());

  String topic = String(HA_DISCOVERY_PREFIX) + "/" + component + "/" +
                 HA_NODE_ID + "/" + obj + "/config";
  String payload;
  serializeJson(doc, payload);
  mqtt.publish(topic.c_str(), payload.c_str(), true);
}

static void publishDiscovery() {
  {  // Power switch
    JsonDocument d;
    d["name"] = "Power";
    d["command_topic"] = tCmd("power");
    d["state_topic"]   = tState("power");
    d["payload_on"] = "ON"; d["payload_off"] = "OFF";
    publishConfig("switch", "power", d);
  }
  {  // Mute switch
    JsonDocument d;
    d["name"] = "Mute";
    d["command_topic"] = tCmd("mute");
    d["state_topic"]   = tState("mute");
    d["payload_on"] = "ON"; d["payload_off"] = "OFF";
    publishConfig("switch", "mute", d);
  }
  {  // Volume number (dB)
    JsonDocument d;
    d["name"] = "Volume";
    d["command_topic"] = tCmd("volume");
    d["state_topic"]   = tState("volume");
    d["min"] = -60; d["max"] = 12; d["step"] = 0.5;
    d["unit_of_measurement"] = "dB";
    d["mode"] = "slider";
    publishConfig("number", "volume", d);
  }
  {  // Source select
    JsonDocument d;
    d["name"] = "Source";
    d["command_topic"] = tCmd("source");
    d["state_topic"]   = tState("source");
    JsonArray opts = d["options"].to<JsonArray>();
    for (int i = 0; i < 18; i++) opts.add(lyngSourceName(i));
    publishConfig("select", "source", d);
  }
  {  // Voicing select
    JsonDocument d;
    d["name"] = "Voicing";
    d["command_topic"] = tCmd("voicing");
    d["state_topic"]   = tState("voicing");
    JsonArray opts = d["options"].to<JsonArray>();
    for (int i = 0; i < 14; i++) opts.add(lyngVoicingName(i));
    publishConfig("select", "voicing", d);
  }
  {  // RoomPerfect select
    JsonDocument d;
    d["name"] = "RoomPerfect";
    d["command_topic"] = tCmd("roomperfect");
    d["state_topic"]   = tState("roomperfect");
    JsonArray opts = d["options"].to<JsonArray>();
    opts.add(lyngRoomPerfectName(0));                  // Bypass
    for (int i = 1; i <= 8; i++) opts.add(lyngRoomPerfectName(i));  // Focus 1-8
    opts.add(lyngRoomPerfectName(9));                  // Global
    publishConfig("select", "roomperfect", d);
  }
  {  // Diagnostic: why the device last rebooted
    JsonDocument d;
    d["name"] = "Last reboot reason";
    d["state_topic"] = tState("last_reset");
    d["icon"] = "mdi:restart-alert";
    d["entity_category"] = "diagnostic";
    publishConfig("sensor", "last_reset", d);
  }
  {  // Diagnostic: most recent WiFi drop (no reboot involved, e.g. AP-side deauth)
    JsonDocument d;
    d["name"] = "Last WiFi drop";
    d["state_topic"] = tState("last_wifi_drop");
    d["icon"] = "mdi:wifi-alert";
    d["entity_category"] = "diagnostic";
    publishConfig("sensor", "last_wifi_drop", d);
  }
}

void mqttPublishState() {
  if (!mqtt.connected()) return;
  if (lyngState.powerKnown)
    mqtt.publish(tState("power").c_str(), lyngState.power ? "ON" : "OFF", true);
  if (lyngState.muteKnown)
    mqtt.publish(tState("mute").c_str(), lyngState.mute ? "ON" : "OFF", true);
  if (lyngState.volKnown) {
    char buf[16]; dtostrf(lyngVolumeDb(), 0, 1, buf);
    mqtt.publish(tState("volume").c_str(), buf, true);
  }
  if (lyngState.srcKnown)
    mqtt.publish(tState("source").c_str(), lyngSourceName(lyngState.source), true);
  if (lyngState.voiKnown)
    mqtt.publish(tState("voicing").c_str(), lyngVoicingName(lyngState.voicing), true);
  if (lyngState.rpKnown)
    mqtt.publish(tState("roomperfect").c_str(),
                 lyngRoomPerfectName(lyngState.rp).c_str(), true);
}

static void onMessage(char* topic, byte* payload, unsigned int len) {
  String t(topic);
  String msg; msg.reserve(len);
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];

  if (t == tCmd("power")) {
    lyngdorfSend(msg.equalsIgnoreCase("ON") ? "!ON" : "!OFF");
  } else if (t == tCmd("mute")) {
    lyngdorfSend(msg.equalsIgnoreCase("ON") ? "!MUTEON" : "!MUTEOFF");
  } else if (t == tCmd("volume")) {
    int raw = (int)roundf(msg.toFloat() * 10.0f);   // dB -> 0.1 dB units
    lyngdorfSend("!VOL(" + String(raw) + ")");
  } else if (t == tCmd("source")) {
    int idx = lyngSourceIndexByName(msg);
    if (idx >= 0) lyngdorfSend("!SRC(" + String(idx) + ")");
  } else if (t == tCmd("voicing")) {
    int idx = lyngVoicingIndexByName(msg);
    if (idx >= 0) lyngdorfSend("!VOI(" + String(idx) + ")");
  } else if (t == tCmd("roomperfect")) {
    String cmd = lyngRoomPerfectCommand(lyngRoomPerfectIndexByName(msg));
    if (cmd.length()) lyngdorfSend(cmd);
  }
}

static String mqttServerHost;   // must outlive PubSubClient: setServer(const char*)
                                // stores the pointer, not a copy.

static void reconnect() {
  if (netMqttHost().isEmpty()) return;
  mqttServerHost = netMqttHost();
  IPAddress ip;
  if (ip.fromString(mqttServerHost))      // numeric IP -> pass by value (safest)
    mqtt.setServer(ip, netMqttPort());
  else                                    // hostname -> keep the String alive
    mqtt.setServer(mqttServerHost.c_str(), netMqttPort());
  mqtt.setBufferSize(1024);   // HA discovery payloads exceed the 256 default
  mqtt.setCallback(onMessage);

  String cid = String(HA_NODE_ID) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok = netMqttUser().isEmpty()
              ? mqtt.connect(cid.c_str(), availTopic().c_str(), 0, true, "offline")
              : mqtt.connect(cid.c_str(), netMqttUser().c_str(),
                             netMqttPass().c_str(), availTopic().c_str(), 0, true,
                             "offline");
  if (!ok) {
    Serial.printf("[mqtt] connect failed rc=%d\n", mqtt.state());
    return;
  }
  Serial.println("[mqtt] connected");
  mqtt.publish(availTopic().c_str(), "online", true);
  mqtt.subscribe(tCmd("power").c_str());
  mqtt.subscribe(tCmd("mute").c_str());
  mqtt.subscribe(tCmd("volume").c_str());
  mqtt.subscribe(tCmd("source").c_str());
  mqtt.subscribe(tCmd("voicing").c_str());
  mqtt.subscribe(tCmd("roomperfect").c_str());
  publishDiscovery();
  mqtt.publish(tState("last_reset").c_str(), diagLastResetText().c_str(), true);
  mqtt.publish(tState("last_wifi_drop").c_str(), diagLastWifiDropText().c_str(), true);
  mqttPublishState();
}

bool mqttConnected() { return mqtt.connected(); }

void mqttApplyConfig() {
  // Settings already persisted by the caller; drop any live connection so
  // mqttLoop() reconnects to the new broker (reconnect() re-reads settings).
  if (mqtt.connected()) mqtt.disconnect();
}

void mqttBegin() {
  lyngdorfSetStateCallback(mqttPublishState);
  if (netMqttHost().isEmpty())
    Serial.println("[mqtt] no broker configured (set it in the portal)");
}

void mqttLoop() {
  if (netMqttHost().isEmpty()) return;
  if (!mqtt.connected()) {
    static uint32_t last = 0;
    if (millis() - last > 5000) { last = millis(); reconnect(); }
    return;
  }
  mqtt.loop();
}
