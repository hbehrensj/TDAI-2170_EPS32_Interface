#include <Arduino.h>
#include "config.h"
#include "net_config.h"
#include "lyngdorf.h"
#include "tcp_bridge.h"
#include "mqtt_ha.h"
#include "mcp_server.h"
#include "ota.h"

// ---- Status LED ----------------------------------------------------------
// Simple non-blocking blink. Pattern conveys the current state.
enum LedMode { LED_BOOT, LED_CONNECTED };
static LedMode ledMode = LED_BOOT;

static inline void ledWrite(bool on) {
#if STATUS_LED_ACTIVE_HIGH
  digitalWrite(STATUS_LED_PIN, on ? HIGH : LOW);
#else
  digitalWrite(STATUS_LED_PIN, on ? LOW : HIGH);
#endif
}

static void statusLedBegin() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  ledWrite(false);
}

static void statusLedLoop() {
  static uint32_t last = 0;
  static bool on = false;
  uint32_t period = (ledMode == LED_CONNECTED) ? 2000 : 250;  // slow when OK
  if (millis() - last >= period) {
    last = millis();
    on = !on;
    ledWrite(on);
  }
}

// USB serial debug console: type a line (e.g. !PWR?) to send it to the amp.
// Works even without WiFi — ideal for bench bring-up. Amp replies print as "<< ...".
static void serialConsoleLoop() {
  static String cmd;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      if (cmd.length()) {
        Serial.printf("[console] sending: %s\n", cmd.c_str());
        lyngdorfSend(cmd);
        cmd = "";
      }
    } else {
      cmd += c;
      if (cmd.length() > 200) cmd = "";
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[TDAI2170] boot");

  statusLedBegin();
  lyngdorfBegin();

  // Connects to WiFi, or runs the captive portal (first boot / BOOT held).
  netConfigBegin();
  ledMode = LED_CONNECTED;

  otaBegin();         // over-the-air firmware updates (espota)
  tcpBridgeBegin();   // raw TCP<->UART bridge on :4001 (Lyngdorf app)
  mqttBegin();        // Home Assistant via MQTT discovery
  mcpBegin();         // MCP server on :80 (/mcp)

  // Subscribe to async status updates and prime current state.
  lyngdorfSend("!SUBSCRIBE");
  lyngdorfSend("!SUBSCRIBEVOL");
  lyngdorfSend("!PWR?");
  lyngdorfSend("!VOL?");
  lyngdorfSend("!SRC?");
  lyngdorfSend("!MUTE?");
  lyngdorfSend("!VOI?");
}

void loop() {
  serialConsoleLoop();
  netConfigLoop();
  otaLoop();
  lyngdorfLoop();
  tcpBridgeLoop();
  mqttLoop();
  mcpLoop();
  statusLedLoop();
}
