#pragma once
#include <WebServer.h>

// Browser-based debug/bring-up UI, registered on the shared WebServer
// (same instance as the MCP server). Served at "/" with polling JSON
// endpoints. Compiled in only when ENABLE_DEBUG_WEB is set.
//
//   GET /             -> HTML debug page
//   GET /api/state    -> JSON: wifi/mqtt/tcp + amplifier state
//   GET /api/log      -> JSON array of recent UART protocol lines
//   GET /api/cmd?c=.. -> send a raw Lyngdorf command

void debugWebRegister(WebServer& server);
