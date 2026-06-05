#pragma once
#include <Arduino.h>

// MQTT client with Home Assistant MQTT Discovery.
// Publishes power (switch), volume (number) and source (select) entities,
// translates HA commands into Lyngdorf serial commands, and publishes
// state back whenever the amp reports a change.

void mqttBegin();
void mqttLoop();
void mqttPublishState();   // registered as the Lyngdorf state callback
bool mqttConnected();      // for the debug UI
void mqttApplyConfig();    // drop the connection so the next loop reconnects
                           // with freshly saved settings (called after web edit)
