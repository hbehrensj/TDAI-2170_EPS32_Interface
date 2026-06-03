#include "mqtt_ha.h"
#include "config.h"
#include "net_config.h"
#include "lyngdorf.h"
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
  }
}

static void reconnect() {
  if (netMqttHost().isEmpty()) return;
  mqtt.setServer(netMqttHost().c_str(), netMqttPort());
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
  publishDiscovery();
  mqttPublishState();
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
