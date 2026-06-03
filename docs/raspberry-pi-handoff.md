# Handoff: Lyngdorf TDAI-2170 controller on Raspberry Pi 4

Brief for a **new project / new session**. Copy this whole file into the new
session as context. It is self-contained — you do not need the old repo to start.

---

## 1. Goal

Build a controller for a **Lyngdorf TDAI-2170** integrated amplifier (RS232 serial
control) on a **Raspberry Pi 4**, exposing four things:

1. **WiFi-to-Serial bridge** — a raw TCP server on **port 4001** so the official
   Lyngdorf phone/tablet app can control the amp over the network (this is exactly
   what [DaftMunk/tdai2170pi](https://github.com/DaftMunk/tdai2170pi) does with
   `ser2net`).
2. **Home Assistant** integration via **MQTT discovery** (the user already runs MQTT
   in HA).
3. A **web page** for status/debug/control.
4. An **embedded MCP server** exposing the amp as tools for AI agents / MCP clients.

## 2. Why we pivoted from the previous attempt

The previous build was an **ESP32-S2 Mini + SP3232** RS232 transceiver. The firmware
was completed and fully proven (WiFi, TCP bridge, MQTT/HA discovery, MCP server, debug
web UI, USB serial console). A clean UART loopback proved the ESP UART + firmware are
100% correct. **The amp comms never worked reliably** — TX reached the amp exactly once
(it toggled power once, could not be reproduced); RX never seen. Diagnosis: the hand-
soldered **SP3232 charge-pump produced marginal RS232 levels** (intermittent). The amp's
RS232 receiver triggers ~±3 V and the weak transceiver only just crossed it occasionally.

Decision: move to a **Pi 4 + a reliable RS232 interface**, where the serial hardware is a
solved problem and the whole stack is plain Linux/Python (far easier than embedded).

The old ESP32 repo (firmware + full protocol doc) remains at:
`github.com/hbehrensj/TDAI-2170_EPS32_Interface` — see `docs/serial-protocol.md` there.

## 3. Hardware — read this carefully (RS232 levels!)

The TDAI-2170 control port is **true RS232** (±5–12 V) on an **RJ12** jack. You must
provide RS232 level conversion. **An FTDI232 / FT232 breakout is USB-to-TTL only — it
does NOT output RS232 levels**, so it cannot drive the amp on its own (this is the same
level problem that sank the ESP build).

Two valid options, in order of preference:

| Option | Notes |
| ------ | ----- |
| **A. USB-to-RS232 adapter (real RS232, FTDI chipset)** — e.g. an FTDI USB-DB9 cable | Cleanest. Has a proper RS232 transceiver inside. Shows up as `/dev/ttyUSB0`. This is the DaftMunk approach. **Recommended.** |
| **B. FT232 (USB-TTL) + a good MAX3232 module** | Works, but adds the same transceiver you just fought with. Only if you specifically want FT232. Use a quality MAX3232 board. |
| C. Pi built-in UART (GPIO14/15 TXD/RXD) + MAX3232 | Also fine; frees a USB port. Needs `enable_uart`, disable serial console. |

Then a **DB9-to-RJ12 cable** (or direct RJ12 wiring) to the amp.

### Serial port settings
`115200` baud, **8 data bits, no parity, 1 stop bit (8N1)**. (Fixed on the TDAI-2170.)

### RJ12 pinout (from the official TDAI-2170 manual, Appendix C)
Amp's RJ12 jack, with the manual's cable colour code:

| RJ12 pin | Colour | Signal (amp's perspective) |
| -------- | ------ | -------------------------- |
| 4 | Green  | **GND** |
| 5 | Yellow | **Rx** (amp input — your TX goes here) |
| 6 | Blue   | **Tx** (amp output — your RX comes from here) |

So your adapter must: **TX → pin 5 (yellow)**, **RX → pin 6 (blue)**, **GND → pin 4
(green)**, using a straight (non-crossing) cable. With a real USB-RS232 adapter you just
wire DB9 TXD→pin5, RXD→pin6, GND→pin4.

## 4. TDAI-2170 serial protocol (everything you need)

ASCII protocol. All commands/requests start with `!` and end with `<CR><LF>` (CR, LF,
CRLF or LFCR all accepted). Parameters in parentheses. Replies mirror the request with
the value as parameter, always terminated `<CR><LF>`. Case-insensitive.

**Subscription:** after `!SUBSCRIBE`, the amp pushes status changes asynchronously,
formatted like a reply but ending with `!` before `<CR><LF>` (e.g. `!SRC(7)!`). Use this
to keep state in sync. `!SUBSCRIBEVOL` adds volume-change pushes.

### Commands
```
!ON                power on
!OFF               power off
!PWR               toggle power (standby)
!VOLUP / !VOLDN    volume ±0.5 dB
!VOLCH(d)          change volume by delta d, in 0.1 dB units, e.g. !VOLCH(-32) = -3.2 dB
!VOL(n)            set volume to n (-999..120), 0.1 dB units (so -200 = -20.0 dB)
!MUTEON / !MUTEOFF / !MUTE     mute on / off / toggle
!SRCUP / !SRCDN    next / previous enabled source
!SRC(n)            select source n (0..17, see below)
!SRCALL(n)         select source n even if disabled
!VOIUP / !VOIDN / !VOI(n)      voicing next / prev / select (0..13)
!RPDN / !RPUP / !RPBP / !RPFOC(n) / !RPGLOB   RoomPerfect controls
!SUBSCRIBE / !UNSUBSCRIBE / !SUBSCRIBEVOL / !UNSUBSCRIBEVOL
```

### Requests → replies
```
!VER?     -> !VER(1.23a)
!DEVICE?  -> !DEVICE(TDAI-2170)
!PWR?     -> !PWR(ON) | !PWR(OFF)
!VOL?     -> !VOL(v)        v in 0.1 dB units, -999..120
!MUTE?    -> !MUTE(ON|OFF)
!SRC?     -> !SRC(n)
!SRCNAME(n)? -> !SRCNAME(n,Name)
!VOI?     -> !VOI(n)
!RP?      -> !RP(n)         0=Bypass, 1-8=Focus, 9=Global
```

### Source numbering (Appendix A)
```
0 Coax Digital 1   1 Coax Digital 2   2 Optical Digital 3   3 Optical Digital 4
4 Optical Digital 5  5 Optical Digital 6  6 USB  7 HDMI 1  8 HDMI 2  9 HDMI 3
10 HDMI 4  11 HDMI ARC  12 Analog 1  13 Analog 2  14 Analog 3  15 Analog 4
16 Analog 5  17 Analog 6 (XLR)
```

### Voicing numbering (Appendix B)
```
0 Neutral  1 Music 1  2 Music 2  3 Relaxed  4 Open  5 Open Air  6 Soft
7 Action 1  8 Action 2  9 Movie  10 Action Movie  11 News  12 Bass 1  13 Bass 2
```

## 5. Architecture (single Python service)

One async Python process owns the serial port and provides all four functions. Do NOT
also run `ser2net` on the same port — only one owner of `/dev/ttyUSB0`. The service
itself provides the raw TCP bridge so the Lyngdorf app still works.

```
                 ┌────────────────── tdai2170d (asyncio) ──────────────────┐
   Lyngdorf app ─┤ TCP :4001  ── raw passthrough ──┐                        │
                 │                                  ├── serial /dev/ttyUSB0 ─┼── RS232 ── amp
   Home Assistant┤ MQTT (discovery) ── translate ──┤  (115200 8N1)          │
                 │                                  │  + line parser ────────┤ tracks state
   Browser ──────┤ HTTP web UI + /api/* ───────────┤                        │ (pwr/mute/vol/src/voi)
   MCP client ───┤ HTTP MCP /mcp (JSON-RPC) ───────┘                        │
                 └──────────────────────────────────────────────────────────┘
```

Core design (mirrors the proven ESP firmware):
- **Serial reader**: read bytes; (a) forward every byte verbatim to all connected TCP
  bridge clients; (b) assemble `!...` lines and parse them to update an in-memory state
  (power, mute, volume, source, voicing). On parsed change → notify MQTT + web/MCP.
- **Serial writer**: a single helper `send(cmd)` appends `\r\n`. All layers call it.
- On startup: `!SUBSCRIBE`, `!SUBSCRIBEVOL`, then `!PWR? !VOL? !SRC? !MUTE? !VOI?`.

## 6. Suggested tech stack

- Python 3.11+, `asyncio`.
- `pyserial-asyncio` for the serial port.
- Raw TCP bridge: `asyncio.start_server` on port 4001.
- MQTT: `aiomqtt` (Home Assistant MQTT discovery).
- Web + MCP: `FastAPI` + `uvicorn`. Web UI = one static page that polls `/api/state`
  and `/api/log`; MCP = a `POST /mcp` JSON-RPC endpoint (or use the official **MCP
  Python SDK** with the Streamable HTTP transport).
- Run as a **systemd** service, auto-start on boot. (DaftMunk used systemd + ser2net.)

## 7. Functional requirements (detail)

### 7.1 WiFi-to-serial bridge (port 4001)
Raw, transparent TCP↔serial passthrough so the official Lyngdorf app works. Allow a
couple of concurrent clients; forward amp→TCP and TCP→amp byte-for-byte. In the app,
add the device manually with the Pi's IP and port 4001.

### 7.2 Home Assistant (MQTT discovery)
Publish discovery configs (retained) under `homeassistant/<component>/tdai2170/<obj>/config`
with a shared `device` block and an `availability_topic`. Entities to create:
- `switch` **power** → `!ON`/`!OFF`, state from `!PWR(...)`
- `switch` **mute** → `!MUTEON`/`!MUTEOFF`, state from `!MUTE(...)`
- `number` **volume** (dB) → set `!VOL(round(db*10))`; state = `v/10` dB. Range e.g.
  -60..+12, step 0.5 (protocol allows down to -99.9).
- `select` **source** → options = the 18 source names; set `!SRC(index)`; state = name.
- (optional) `select` **voicing** (14 names), `select` **RoomPerfect**.
Subscribe to the `.../set` command topics; translate to serial; publish state on every
parsed change.

### 7.3 Web page
Status (wifi/serial/MQTT, live amp state), a **live serial log** (ring buffer of recent
TX `>>` / RX `<<` lines — invaluable for bring-up), quick buttons (power/mute/vol/source)
and a raw-command box. Poll `/api/state` + `/api/log` every ~1 s.

### 7.4 MCP server
JSON-RPC 2.0 over HTTP (Streamable HTTP transport). Methods: `initialize`, `tools/list`,
`tools/call`. Tools: `get_state`, `set_power{state:on|off|toggle}`, `set_volume{db}`,
`volume_step{direction}`, `select_source{index|name}`, `select_voicing{index}`,
`send_raw{command}`. Use the latest Claude models when building any AI side.

## 8. Lessons learned / gotchas (from the ESP attempt)

- **RS232 levels are the whole game.** Don't use a TTL-only adapter against the amp. A
  real USB-RS232 adapter (or a quality MAX3232) is the reliable path. The previous failure
  was a marginal hand-built charge pump → intermittent comms.
- **Port 4001 + raw passthrough** is what makes the official Lyngdorf app work
  (ser2net `115200N81` in DaftMunk). One process must own the serial port and provide
  the bridge itself; don't run ser2net alongside another serial consumer.
- **RJ12 wiring**: amp pin4=GND(green), pin5=Rx(yellow), pin6=Tx(blue); straight cable;
  your TX→pin5, RX→pin6. Match by **wire colour**, not by counting pins (pin 1/6 are easy
  to flip depending on connector orientation).
- **`!SUBSCRIBE`** gives async state pushes — use it instead of polling to keep HA/MCP/web
  state fresh.
- Volume is in **0.1 dB units** in the protocol (`!VOL(-200)` = -20.0 dB).
- A **live serial log in the web UI** was the single most useful debugging tool — build it
  early. Distinguish `>>` (sent) and `<<` (received); "no `<<` ever" instantly tells you
  the RX path (or the amp's reply) is the problem.
- On the Pi, verify the serial link first with a one-liner before building everything:
  `python3 -c "import serial,time; s=serial.Serial('/dev/ttyUSB0',115200,timeout=1); s.write(b'!PWR?\r\n'); print(s.read(64))"`
  — a reply like `!PWR(ON)` proves the hardware end-to-end.

## 9. First milestone
Get `!PWR?` → `!PWR(ON|OFF)` over the real serial hardware (step 8 one-liner). Once that
works, the rest is straightforward Python. Then build, in order: serial owner + state
parser → TCP bridge (4001) → web UI with live log → MQTT/HA → MCP.
