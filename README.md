# TDAI-2170 — ESP32-S2 WiFi-to-Serial Interface

A WiFi-to-Serial adapter built on an **ESP32-S2 Mini**, used to control a
**Lyngdorf TDAI-2170** integrated amplifier over its RS232 serial interface —
wirelessly over WiFi. It also runs an **embedded MCP server** so the amplifier
can be controlled directly from an MCP client / AI agent, a **Home Assistant**
MQTT integration, a browser debug UI, and **over-the-air (OTA) firmware updates**.

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

ESP32-S2 Mini ←→ SP3232/MAX3232 converter ←→ RJ12 connector.

The converter's TTL pins follow the **cross convention** (board TXD → MCU RX,
board RXD → MCU TX), so in firmware the ESP **transmits on GPIO 18** and
**receives on GPIO 17** (see `LYNGDORF_TX_PIN`/`LYNGDORF_RX_PIN` in
[src/config.h](src/config.h)). Getting this backwards is silent — nothing works.

| Board label   | Connects to                        |
| ------------- | ---------------------------------- |
| VCC           | ESP32 3.3V                         |
| GND (TTL)     | ESP32 GND                          |
| TXD (TTL)     | ESP32 GPIO 17  (= firmware **RX**) |
| RXD (TTL)     | ESP32 GPIO 18  (= firmware **TX**) |
| TXD (RS232)   | RJ12 Pin 5  (amp Rx, yellow)       |
| RXD (RS232)   | RJ12 Pin 6  (amp Tx, blue)         |
| GND (RS232)   | RJ12 Pin 4  (GND, green)           |

RJ12 (amp side, from the manual): **pin 4 = GND (green), pin 5 = Rx (yellow),
pin 6 = Tx (blue)**, straight (non-crossing) cable.

> The hardware is built and verified end to end (the amp replies, e.g.
> `!VER?` → `!VER(1.39a)`). The ESP talks UART to the converter, which
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

## Features

- [x] WiFi configuration mode (captive-portal AP, triggered by BOOT button on GPIO 0)
- [x] Persist WiFi credentials in NVS, with an auto-reconnect watchdog
- [x] mDNS/Bonjour — reachable at `tdai2170.local`
- [x] TCP server on port 4001 bridging raw bytes to UART (Lyngdorf-app compatible)
- [x] Embedded MCP server with high-level tools mapped to TDAI-2170 commands
      (power, volume, source, voicing, raw)
- [x] Home Assistant MQTT discovery (Power, Mute, Volume, Source entities)
- [x] Runtime MQTT configuration from the web UI (no recompile / re-portal needed)
- [x] Browser debug UI with live state + UART log
- [x] **OTA (over-the-air) firmware updates** over WiFi (espota)
- [x] Status LED indicating mode (config / connecting / connected)
- [ ] Self-update from GitHub releases (see *Releases & self-update* below)

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
  ota.*                 Over-the-air firmware updates (espota)
```

`include/secrets.h` (gitignored) can hold a hardcoded WiFi SSID/password for
bench bring-up; **leave `WIFI_SSID` empty for normal use** so the device uses
the WiFiManager portal / stored home-network credentials.

## Getting started

Built with [PlatformIO](https://platformio.org/).

```bash
pio run                 # compile
pio run --target upload # flash over USB (first time)
pio run -e ota -t upload # flash wirelessly once OTA is on the device
pio device monitor      # serial logs (115200)
```

> **OTA:** after the first USB flash, subsequent updates can be pushed over WiFi
> with `pio run -e ota -t upload` (targets `tdai2170.local`, password
> `tdai2170-ota` — override with `-D OTA_PASSWORD=...`). Handy on the ESP32-S2
> Mini, whose native USB drops its serial port on every reset.

### First-time setup

1. Flash and power the board. On first boot (no stored WiFi) it starts a
   captive-portal AP named **`TDAI2170-Setup`**.
2. Connect to that AP, open the portal, and enter your **WiFi** credentials plus
   the **MQTT broker** host/port/user/password (for Home Assistant).
3. The device saves the settings to NVS, reboots, and connects. To re-enter setup
   later, **hold the BOOT button (~3 s)**.

> **MQTT can also be (re)configured at runtime** from the web UI at
> `http://tdai2170.local/` (the *MQTT settings* section) — no portal or reflash
> needed. Settings persist to NVS and the client reconnects immediately.

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

## Releases & self-update

Two distinct mechanisms — don't confuse them:

- **espota (implemented)** — *push* updates from your machine on the LAN with
  `pio run -e ota -t upload`. You initiate the flash; the device is the target.
- **Self-update from GitHub (planned)** — the device *pulls* a new firmware
  image itself. This needs (1) a CI workflow that builds `firmware.bin` and a
  small `version.json` manifest and attaches them to a GitHub Release, and
  (2) on-device code (ESP32 `HTTPUpdate` over TLS) that periodically checks the
  manifest and downloads the binary when a newer version is published. See the
  checklist item above; not yet built.

## Status

🚧 Under development — core firmware working end to end.

- ✅ Hardware built (ESP32-S2 + TTL↔RS232 + RJ12), amp verified
- ✅ WiFi bridge (TCP :4001), captive-portal config, mDNS, auto-reconnect
- ✅ MCP server (`/mcp`), Home Assistant MQTT discovery, browser debug UI
- ✅ OTA firmware updates (espota)
- ⏳ Self-update from GitHub releases

## License

Not yet decided.
