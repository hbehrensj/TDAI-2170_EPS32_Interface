#include "mcp_server.h"
#include "config.h"
#include "lyngdorf.h"
#include "debug_web.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

static WebServer server(MCP_HTTP_PORT);

static const char* MCP_PROTOCOL_VERSION = "2025-06-18";

// ---- tool implementations ------------------------------------------------
// Each returns a human-readable text result for tools/call content.

static String toolGetState() {
  JsonDocument d;
  d["power"]   = lyngState.powerKnown ? (lyngState.power ? "on" : "off") : "unknown";
  d["mute"]    = lyngState.muteKnown ? (lyngState.mute ? "on" : "off") : "unknown";
  d["volume_db"] = lyngState.volKnown ? lyngVolumeDb() : (float)NAN;
  d["source"]  = lyngState.srcKnown ? lyngSourceName(lyngState.source) : "unknown";
  d["voicing"] = lyngState.voiKnown ? lyngVoicingName(lyngState.voicing) : "unknown";
  d["roomperfect"] = lyngState.rpKnown ? lyngRoomPerfectName(lyngState.rp) : "unknown";
  if (lyngState.version.length()) d["version"] = lyngState.version;
  if (lyngState.device.length())  d["device"]  = lyngState.device;
  d["audio_format"] = lyngState.audioKnown
      ? lyngAudioFormatText(lyngState.audioBitDepthCode, lyngState.audioSampleRateCode)
      : "unknown";
  d["audio_level_db"] = lyngState.audioKnown
      ? (lyngState.audioLevelDb <= -999 ? "silence" : String(lyngState.audioLevelDb / 10.0f, 1) + " dB")
      : "unknown";
  String out; serializeJson(d, out); return out;
}

static String toolCall(const String& name, JsonObject args) {
  if (name == "get_state") {
    return toolGetState();
  } else if (name == "set_power") {
    String s = args["state"] | "toggle";
    if (s == "on")  lyngdorfSend("!ON");
    else if (s == "off") lyngdorfSend("!OFF");
    else lyngdorfSend("!PWR");
    return "power -> " + s;
  } else if (name == "set_volume") {
    float db = args["db"] | 0.0f;
    int raw = (int)roundf(db * 10.0f);
    lyngdorfSend("!VOL(" + String(raw) + ")");
    return "volume -> " + String(db, 1) + " dB";
  } else if (name == "volume_step") {
    String dir = args["direction"] | "up";
    lyngdorfSend(dir == "down" ? "!VOLDN" : "!VOLUP");
    return "volume step " + dir;
  } else if (name == "change_volume") {
    float db = args["db"] | 0.0f;
    int raw = (int)roundf(db * 10.0f);          // 0.1 dB units
    lyngdorfSend("!VOLCH(" + String(raw) + ")");
    return "volume change " + String(db, 1) + " dB";
  } else if (name == "set_mute") {
    String s = args["state"] | "toggle";
    if (s == "on")  lyngdorfSend("!MUTEON");
    else if (s == "off") lyngdorfSend("!MUTEOFF");
    else lyngdorfSend("!MUTE");
    return "mute -> " + s;
  } else if (name == "select_roomperfect") {
    int n = -1;
    if (args["index"].is<int>()) n = args["index"];
    else n = lyngRoomPerfectIndexByName(args["name"] | "");
    String cmd = lyngRoomPerfectCommand(n);
    if (cmd.isEmpty()) return "error: invalid RoomPerfect position (0=Bypass,1-8=Focus,9=Global)";
    lyngdorfSend(cmd);
    return "roomperfect -> " + lyngRoomPerfectName(n);
  } else if (name == "select_source") {
    if (args["index"].is<int>()) {
      int n = args["index"];
      lyngdorfSend("!SRC(" + String(n) + ")");
      return "source -> " + String(lyngSourceName(n));
    }
    String nm = args["name"] | "";
    int idx = lyngSourceIndexByName(nm);
    if (idx < 0) return "error: unknown source name";
    lyngdorfSend("!SRC(" + String(idx) + ")");
    return "source -> " + nm;
  } else if (name == "select_voicing") {
    int n;
    if (args["index"].is<int>()) n = args["index"];
    else { n = lyngVoicingIndexByName(args["name"] | ""); }
    if (n < 0) return "error: unknown voicing";
    lyngdorfSend("!VOI(" + String(n) + ")");
    return "voicing -> " + String(lyngVoicingName(n));
  } else if (name == "send_raw") {
    String cmd = args["command"] | "";
    if (cmd.isEmpty()) return "error: empty command";
    lyngdorfSend(cmd);
    return "sent: " + cmd;
  }
  return "error: unknown tool";
}

// ---- tools/list schema ---------------------------------------------------
static void addTool(JsonArray tools, const char* name, const char* desc,
                    JsonDocument& schema) {
  JsonObject t = tools.add<JsonObject>();
  t["name"] = name;
  t["description"] = desc;
  t["inputSchema"] = schema;
}

static void buildToolsList(JsonArray tools) {
  { JsonDocument s; s["type"] = "object"; s["properties"].to<JsonObject>();
    addTool(tools, "get_state", "Get current amplifier state", s); }
  { JsonDocument s; s["type"] = "object";
    JsonObject p = s["properties"].to<JsonObject>();
    p["state"]["type"] = "string";
    p["state"]["enum"][0] = "on"; p["state"]["enum"][1] = "off";
    p["state"]["enum"][2] = "toggle";
    addTool(tools, "set_power", "Turn the amplifier on/off/toggle", s); }
  { JsonDocument s; s["type"] = "object";
    s["properties"]["db"]["type"] = "number";
    s["required"][0] = "db";
    addTool(tools, "set_volume", "Set absolute volume in dB", s); }
  { JsonDocument s; s["type"] = "object";
    JsonObject p = s["properties"].to<JsonObject>();
    p["direction"]["type"] = "string";
    p["direction"]["enum"][0] = "up"; p["direction"]["enum"][1] = "down";
    addTool(tools, "volume_step", "Step volume up/down by 0.5 dB", s); }
  { JsonDocument s; s["type"] = "object";
    s["properties"]["db"]["type"] = "number";
    s["required"][0] = "db";
    addTool(tools, "change_volume", "Change volume by a relative amount in dB (e.g. -3.0)", s); }
  { JsonDocument s; s["type"] = "object";
    JsonObject p = s["properties"].to<JsonObject>();
    p["state"]["type"] = "string";
    p["state"]["enum"][0] = "on"; p["state"]["enum"][1] = "off";
    p["state"]["enum"][2] = "toggle";
    addTool(tools, "set_mute", "Mute/unmute/toggle the amplifier", s); }
  { JsonDocument s; s["type"] = "object";
    JsonObject p = s["properties"].to<JsonObject>();
    p["index"]["type"] = "integer";
    p["name"]["type"] = "string";
    addTool(tools, "select_source", "Select input source by index (0-17) or name", s); }
  { JsonDocument s; s["type"] = "object";
    JsonObject p = s["properties"].to<JsonObject>();
    p["index"]["type"] = "integer";
    p["name"]["type"] = "string";
    addTool(tools, "select_voicing", "Select voicing by index (0-13) or name", s); }
  { JsonDocument s; s["type"] = "object";
    JsonObject p = s["properties"].to<JsonObject>();
    p["index"]["type"] = "integer";
    p["name"]["type"] = "string";
    addTool(tools, "select_roomperfect",
            "Select RoomPerfect position by index (0=Bypass, 1-8=Focus, 9=Global) or name", s); }
  { JsonDocument s; s["type"] = "object";
    s["properties"]["command"]["type"] = "string";
    s["required"][0] = "command";
    addTool(tools, "send_raw", "Send a raw Lyngdorf command, e.g. !VOL(-200)", s); }
}

// ---- JSON-RPC dispatch ---------------------------------------------------
static void sendResult(JsonVariant id, JsonDocument& result) {
  JsonDocument resp;
  resp["jsonrpc"] = "2.0";
  resp["id"] = id;
  resp["result"] = result;
  String out; serializeJson(resp, out);
  server.send(200, "application/json", out);
}

static void sendError(JsonVariant id, int code, const char* message) {
  JsonDocument resp;
  resp["jsonrpc"] = "2.0";
  resp["id"] = id;
  resp["error"]["code"] = code;
  resp["error"]["message"] = message;
  String out; serializeJson(resp, out);
  server.send(200, "application/json", out);
}

static void handleMcp() {
  JsonDocument req;
  if (deserializeJson(req, server.arg("plain"))) {
    server.send(400, "application/json",
                "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32700,\"message\":\"parse error\"}}");
    return;
  }
  String method = req["method"] | "";
  JsonVariant id = req["id"];

  if (method == "initialize") {
    JsonDocument r;
    r["protocolVersion"] = MCP_PROTOCOL_VERSION;
    r["capabilities"]["tools"].to<JsonObject>();
    r["serverInfo"]["name"] = "tdai2170";
    r["serverInfo"]["version"] = "0.1.0";
    sendResult(id, r);
  } else if (method == "notifications/initialized") {
    server.send(202, "application/json", "{}");
  } else if (method == "tools/list") {
    JsonDocument r;
    buildToolsList(r["tools"].to<JsonArray>());
    sendResult(id, r);
  } else if (method == "tools/call") {
    String name = req["params"]["name"] | "";
    JsonObject args = req["params"]["arguments"].as<JsonObject>();
    String text = toolCall(name, args);
    JsonDocument r;
    JsonObject c = r["content"].add<JsonObject>();
    c["type"] = "text";
    c["text"] = text;
    r["isError"] = text.startsWith("error:");
    sendResult(id, r);
  } else {
    sendError(id, -32601, "method not found");
  }
}

void mcpBegin() {
  server.on("/mcp", HTTP_POST, handleMcp);
#if ENABLE_DEBUG_WEB
  debugWebRegister(server);   // adds "/" debug UI + /api/* on the same server
#else
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/plain",
                "TDAI-2170 MCP server. POST JSON-RPC to /mcp");
  });
#endif
  server.begin();
  Serial.printf("[mcp] HTTP server on :%d  (POST /mcp)\n", MCP_HTTP_PORT);
}

void mcpLoop() { server.handleClient(); }

// Release port 80 so WiFiManager's config portal can bind it when the
// portal is entered at runtime (after mcpBegin() has already started).
void mcpStop() {
  server.stop();
  Serial.println("[mcp] HTTP server stopped (port 80 released)");
}
