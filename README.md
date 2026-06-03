# TDAI-2170 — ESP32-S2 WiFi-to-Serial Interface

A WiFi-to-Serial adapter built on an **ESP32-S2 Mini**, used to control a
**Lyngdorf TDAI-2170** integrated amplifier over its RS232 serial interface —
wirelessly over WiFi. The project will be extended with an **embedded MCP server**
so the amplifier can be controlled directly from an MCP client / AI agent.

## Goals

- 📡 **WiFi-to-Serial bridge** — transparent TCP-to-UART bridge so the official
  Lyngdorf app (and other RS232 controllers) can reach the amplifier over WiFi.
- 🔌 **ESP32-S2 Mini** as the hardware platform.
- 🛜 **On-device WiFi configuration mode** — triggered by a button press, no recompile
  needed to join a new network.
- 🤖 **Embedded MCP server** — expose the amplifier's functions as MCP tools so they
  can be used by AI agents and MCP clients.

## Prior art / inspiration

[DaftMunk/tdai2170pi](https://github.com/DaftMunk/tdai2170pi) does the same core job on
a **Raspberry Pi**: it runs [`ser2net`](https://github.com/cminyard/ser2net) to expose a
TCP serial server on **port 4001** (`telnet`/RFC2217, `115200N81`) that the official
Lyngdorf app connects to and routes to `/dev/ttyUSB0`.

This project reimplements that bridge on an **ESP32-S2** (no Linux, no USB dongle) and
adds an embedded MCP server. To stay compatible with the Lyngdorf app, the ESP32 exposes
a raw **TCP server on port 4001** that forwards bytes verbatim to/from the UART.

The main thing the move from Raspberry Pi to ESP32 adds: there is no SSH/keyboard to set
up WiFi, so the firmware needs a **WiFi configuration mode** (captive-portal AP) that can
be entered by pressing a button on the board — see below.

## Hardware

| Component        | Detail                                  |
| ---------------- | --------------------------------------- |
| Microcontroller  | ESP32-S2 Mini (Lolin/WeMos)             |
| Level converter  | TTL ↔ RS232 (MAX3232-type)              |
| Connector        | RJ12 to the Lyngdorf TDAI-2170          |
| Connectivity     | WiFi (2.4 GHz)                          |
| Target device    | Lyngdorf TDAI-2170 integrated amplifier |

### Pinout / wiring

ESP32-S2 Mini ←→ TTL↔RS232 converter ←→ RJ12 connector:

| Board label   | Connects to           |
| ------------- | --------------------- |
| VCC           | ESP32 3.3V            |
| GND (TTL)     | ESP32 GND             |
| TXD (TTL)     | ESP32 GPIO 17 (TX)    |
| RXD (TTL)     | ESP32 GPIO 18 (RX)    |
| TXD (RS232)   | RJ12 Pin 6            |
| RXD (RS232)   | RJ12 Pin 5            |
| GND (RS232)   | RJ12 Pin 4            |

> The hardware is built. The ESP32 talks UART (GPIO 17/18) to the converter, which
> translates to RS232 and routes it to the RJ12 connector matching the Lyngdorf pinout.

## Serial protocol

The TDAI-2170 uses a simple ASCII serial protocol. Full details and the complete
command set are documented in [docs/serial-protocol.md](docs/serial-protocol.md).

**Serial settings:** `115200` baud, `8` data bits, no parity, `1` stop bit (8N1).

Commands/requests start with `!` and end with `<CR><LF>`. Examples:

```
!ON          # power on
!OFF         # power off
!VOL?        # query volume  -> !VOL(v)
!VOLUP       # volume +0.5 dB
!SRC(7)      # select HDMI Input 1
!SUBSCRIBE   # push status changes asynchronously
```

## WiFi configuration mode

Because the ESP32 has no console to configure WiFi, the firmware boots into one of two modes:

1. **Normal mode** — connects to the stored WiFi network and starts the TCP bridge
   (port 4001) and the MCP server.
2. **Configuration mode** — starts a SoftAP + captive portal where you enter the WiFi
   SSID/password from a phone/laptop. Credentials are saved to NVS and the device reboots
   into normal mode.

Config mode is entered when:

- No WiFi credentials are stored yet (first boot), **or**
- The **BOOT button (GPIO 0)** on the ESP32-S2 Mini is held during/after startup.

> The ESP32-S2 Mini exposes the **BOOT** button on GPIO 0 and **RST**. GPIO 0 is the
> natural choice for the "enter WiFi setup" trigger (hold for ~3 s). A status LED pattern
> indicates which mode the device is in.

## Features (planned)

- [ ] WiFi configuration mode (captive-portal AP, triggered by BOOT button on GPIO 0)
- [ ] Persist WiFi credentials in NVS
- [ ] TCP server on port 4001 bridging raw bytes to UART (Lyngdorf-app compatible)
- [ ] Configurable baud rate (default 115200 for TDAI-2170)
- [ ] Embedded MCP server with tools for serial I/O
- [ ] High-level MCP tools mapped to TDAI-2170 commands (power, volume, source, voicing)
- [ ] Status LED indicating mode (config / connecting / connected)

## Project layout

```
platformio.ini          PlatformIO config (board: lolin_s2_mini)
docs/serial-protocol.md  Full TDAI-2170 RS232 command reference
src/
  config.h              Pins, ports, MQTT/HA identity
  main.cpp              Boot/loop orchestration + status LED
  net_config.*          WiFi connect + captive portal (BOOT button) + NVS
  lyngdorf.*            UART driver + '!' protocol parser + state
  tcp_bridge.*          Raw TCP<->UART bridge on port 4001
  mqtt_ha.*             MQTT client + Home Assistant discovery
  mcp_server.*          MCP server (JSON-RPC over HTTP) at /mcp
  debug_web.*           Browser debug UI + /api/* (shares the HTTP server)
```

## Getting started

Built with [PlatformIO](https://platformio.org/).

```bash
pio run                 # compile
pio run --target upload # flash over USB
pio device monitor      # serial logs (115200)
```

### First-time setup

1. Flash and power the board. On first boot (no stored WiFi) it starts a
   captive-portal AP named **`TDAI2170-Setup`**.
2. Connect to that AP, open the portal, and enter your **WiFi** credentials plus
   the **MQTT broker** host/port/user/password (for Home Assistant).
3. The device saves the settings to NVS, reboots, and connects. To re-enter setup
   later, **hold the BOOT button (~3 s)**.

### Finding the device

The firmware advertises itself over mDNS/Bonjour, so once connected it is reachable
by name at **`http://tdai2170.local/`** (works on macOS/iOS, Linux/avahi, Windows 10+).
The IP is also printed over USB serial at boot, and the DHCP hostname is `tdai2170`.

### Endpoints once connected

- **HTTP `:80` `/`** — browser **debug UI**: live state, UART protocol log, quick
  commands and a raw-command box. Great for first-time bring-up. Disable with
  `-D ENABLE_DEBUG_WEB=0`.
- **TCP `:4001`** — point the official Lyngdorf app here (raw serial bridge).
- **HTTP `:80` `/mcp`** — MCP JSON-RPC endpoint for AI agents / MCP clients.
- **MQTT** — Home Assistant auto-discovers Power, Mute, Volume and Source entities.

## Status

🚧 Under development.

- ✅ Hardware built (ESP32-S2 + TTL↔RS232 + RJ12)
- ⏳ Firmware (WiFi bridge + MCP server)

## License

Not yet decided.
