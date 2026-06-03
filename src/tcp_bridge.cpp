#include "tcp_bridge.h"
#include "config.h"
#include "lyngdorf.h"
#include <WiFi.h>

#define MAX_CLIENTS 2

static WiFiServer server(TCP_BRIDGE_PORT);
static WiFiClient clients[MAX_CLIENTS];

void tcpBridgeBegin() {
  server.begin();
  server.setNoDelay(true);
  lyngdorfSetRawSink(tcpBridgePush);   // UART -> TCP
  Serial.printf("[tcp] bridge listening on :%d\n", TCP_BRIDGE_PORT);
}

void tcpBridgePush(const uint8_t* data, size_t len) {
  for (int i = 0; i < MAX_CLIENTS; i++)
    if (clients[i] && clients[i].connected())
      clients[i].write(data, len);
}

void tcpBridgeLoop() {
  // Accept new connections.
  if (server.hasClient()) {
    int slot = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (!clients[i] || !clients[i].connected()) { slot = i; break; }
    }
    WiFiClient incoming = server.available();
    if (slot >= 0) {
      clients[slot] = incoming;
      clients[slot].setNoDelay(true);
      Serial.printf("[tcp] client connected (slot %d) %s\n",
                    slot, incoming.remoteIP().toString().c_str());
    } else {
      incoming.stop();   // full
    }
  }

  // TCP -> UART.
  uint8_t buf[256];
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (!clients[i] || !clients[i].connected()) continue;
    int avail = clients[i].available();
    while (avail > 0) {
      int n = clients[i].read(buf, min(avail, (int)sizeof(buf)));
      if (n <= 0) break;
      lyngdorfWriteRaw(buf, n);
      avail -= n;
    }
  }
}
