#pragma once
#include <Arduino.h>

// Transparent TCP <-> UART bridge on TCP_BRIDGE_PORT (4001).
// Mirrors the ser2net setup used by DaftMunk/tdai2170pi so the official
// Lyngdorf app can connect over WiFi. Bytes are forwarded verbatim in
// both directions; no protocol interpretation happens here.

void tcpBridgeBegin();
void tcpBridgeLoop();

// Registered as the Lyngdorf raw sink: push UART bytes out to TCP clients.
void tcpBridgePush(const uint8_t* data, size_t len);
