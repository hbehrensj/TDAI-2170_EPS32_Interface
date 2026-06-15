#pragma once
// Independent software watchdog. An esp_timer task (high priority) checks a
// heartbeat that loop() refreshes; if loop() stalls, the device reboots itself.
// This recovers a wedged loop() that the WiFi/MQTT reconnect logic can't,
// because that logic only runs *from* loop().
//
// Long but legitimate blocking operations (config portal, self-update download,
// incoming OTA flash) must bracket themselves with watchdogPause()/Resume() so
// they are not mistaken for a stall.

void watchdogBegin();    // start the watchdog (call once, after setup() work)
void watchdogFeed();     // heartbeat — call every loop() iteration
void watchdogPause();    // suspend checking around a known long blocking call
void watchdogResume();   // re-arm after the blocking call returns
