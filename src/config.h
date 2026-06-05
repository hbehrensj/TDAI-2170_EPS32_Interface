#pragma once
// Central hardware / network configuration for the TDAI-2170 interface.

// ---- UART to the TTL<->RS232 converter (-> RJ12 -> Lyngdorf) -------------
// NOTE: the SP3232 board uses the "cross" convention on the TTL side:
//   board TXD -> MCU RX,  board RXD -> MCU TX.
// The wiring is ESP17<->board TXD and ESP18<->board RXD, so to talk correctly
// the ESP must TRANSMIT on 18 (-> board RXD/input) and RECEIVE on 17 (<- board TXD/output).
#define LYNGDORF_TX_PIN     18        // ESP GPIO 18 -> board RXD (board's TTL input)
#define LYNGDORF_RX_PIN     17        // ESP GPIO 17 <- board TXD (board's TTL output)
#define LYNGDORF_BAUD       115200    // TDAI-2170 fixed: 115200 8N1
#define LYNGDORF_UART_NUM   1         // use UART1 (UART0 is USB CDC on S2)

// ---- Buttons / LED -------------------------------------------------------
// The LOLIN S2 Mini exposes the BOOT button on GPIO 0 (active LOW).
#define CONFIG_BUTTON_PIN   0
#define CONFIG_HOLD_MS      3000      // hold BOOT this long to enter WiFi setup

// Built-in LED. NOTE: verify the pin for your exact board revision.
#define STATUS_LED_PIN      15
#define STATUS_LED_ACTIVE_HIGH 1

// ---- Network services ----------------------------------------------------
#define TCP_BRIDGE_PORT     4001      // raw TCP<->UART bridge (Lyngdorf app)
#define MCP_HTTP_PORT       80        // MCP server (Streamable HTTP / JSON-RPC)
#define WIFI_AP_NAME        "TDAI2170-Setup"

// MQTT / Home Assistant identity
#define HA_NODE_ID          "tdai2170"
#define HA_DEVICE_NAME      "Lyngdorf TDAI-2170"
#define HA_DISCOVERY_PREFIX "homeassistant"

// mDNS hostname -> reachable as http://<MDNS_HOSTNAME>.local/
#define MDNS_HOSTNAME       HA_NODE_ID

// ---- OTA (over-the-air firmware updates) ---------------------------------
// Password required by espota uploads. Override with -D OTA_PASSWORD=... .
#ifndef OTA_PASSWORD
#define OTA_PASSWORD        "tdai2170-ota"
#endif

// ---- Firmware version + self-update from GitHub releases -----------------
// The release CI injects the git tag with  -D FIRMWARE_VERSION=1.5.0  (no
// quotes); FIRMWARE_VERSION_STR stringifies it. Local/dev builds use the
// default below.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION_STR "1.4.0"
#else
#define FW_STR2(x) #x
#define FW_STR(x)  FW_STR2(x)
#define FIRMWARE_VERSION_STR FW_STR(FIRMWARE_VERSION)
#endif

// Stable "latest release" asset URLs (always point at the newest release).
#define UPDATE_VERSION_URL \
  "https://github.com/hbehrensj/TDAI-2170_EPS32_Interface/releases/latest/download/version.json"
#define UPDATE_FIRMWARE_URL \
  "https://github.com/hbehrensj/TDAI-2170_EPS32_Interface/releases/latest/download/firmware.bin"

// How often the device auto-checks GitHub for a newer release.
#define UPDATE_CHECK_INTERVAL_MS  (24UL * 60 * 60 * 1000)   // daily

// ---- Debug web UI --------------------------------------------------------
// Browser-based debug page at "/" (state, live UART log, command box).
// Disable in production with build flag  -D ENABLE_DEBUG_WEB=0
#ifndef ENABLE_DEBUG_WEB
#define ENABLE_DEBUG_WEB    1
#endif
