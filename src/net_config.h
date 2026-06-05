#pragma once
#include <Arduino.h>

// WiFi connection + on-device configuration portal.
//
// Boots into one of two modes:
//   * Normal      - connects to stored WiFi, starts services.
//   * Config (AP) - captive portal to enter WiFi + MQTT settings.
//
// Config mode is entered on first boot (no creds) or by holding the
// BOOT button (GPIO 0) for CONFIG_HOLD_MS.

void netConfigBegin();   // blocks until connected (may run captive portal)
void netConfigLoop();    // handles button hold -> re-enter portal, reconnect

// MQTT settings collected in the portal and persisted in NVS.
String   netMqttHost();
uint16_t netMqttPort();
String   netMqttUser();
String   netMqttPass();

// Update + persist MQTT settings at runtime (from the web UI). An empty
// `pass` leaves the stored password unchanged. Triggers an MQTT reconnect.
void netSetMqtt(const String& host, const String& port,
                const String& user, const String& pass);
