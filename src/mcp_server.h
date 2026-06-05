#pragma once
#include <Arduino.h>

// Embedded MCP server exposing the TDAI-2170 as tools to MCP clients.
// Implemented as a JSON-RPC 2.0 endpoint over HTTP (MCP Streamable HTTP
// transport) at  POST http://<device>/mcp .
//
// Supported methods: initialize, tools/list, tools/call.
// Tools: get_state, set_power, set_volume, volume_step, select_source,
//        select_voicing, send_raw.

void mcpBegin();
void mcpLoop();
void mcpStop();   // release port 80 (e.g. before opening the config portal)
