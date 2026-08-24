# Lyngdorf TDAI-2170 — Serial Control Protocol

Reference for the RS232 serial control interface of the Lyngdorf TDAI-2170
integrated amplifier. Source: *TDAI-2170 External Control Manual* (Lyngdorf, March 2018).

## Serial settings

| Setting   | Value  |
| --------- | ------ |
| Baud rate | 115200 |
| Data bits | 8      |
| Parity    | None   |
| Stop bits | 1      |

## RJ12 pinout

| RJ12 pin | Signal      |
| -------- | ----------- |
| 4        | GND (RS232) |
| 5        | RXD (RS232) |
| 6        | TXD (RS232) |

## Message format

All commands and requests start with the `!` character.

| Form                       | Meaning                          |
| -------------------------- | -------------------------------- |
| `!COMMAND<CR><LF>`         | Command                          |
| `!COMMAND(param)<CR><LF>`  | Command with parameter           |
| `!REQUEST?<CR><LF>`        | Request                          |
| `!REQUEST(param)?<CR><LF>` | Request with parameter           |
| `!REQUEST(reply)<CR><LF>`  | Reply to a request               |
| `!REQUEST(status)!<CR><LF>`| Asynchronous status (subscribed) |

Notes:

- Line ending: `<CR>` (0x0D), `<LF>` (0x0A), `<CR><LF>` or `<LF><CR>` are all accepted.
  Replies always use `<CR><LF>`.
- Parameters are enclosed in parentheses.
- Commands/requests are **not** case sensitive.
- Malformed commands are silently ignored. Trailing garbage after a valid command
  before the line end is ignored (the command still executes).
- Asynchronous status messages look like a reply but end with `!` before `<CR><LF>`.

## Requests

| Request           | Action                          | Reply                                                  |
| ----------------- | ------------------------------- | ------------------------------------------------------ |
| `!VER?`           | Software version                | `!VER(1.23a)`                                          |
| `!DEVICE?`        | Device type                     | `!DEVICE(TDAI-2170)`                                   |
| `!PWR?`           | Power state                     | `!PWR(ON)` / `!PWR(OFF)`                               |
| `!VOL?`           | Current volume                  | `!VOL(v)` — v from -999 to 120 in 0.1 dB steps         |
| `!MUTE?`          | Mute status                     | `!MUTE(ON)` / `!MUTE(OFF)`                             |
| `!SRC?`           | Selected input source           | `!SRC(n)` — see Appendix A                             |
| `!SRCNAME(n)?`    | Name of input source n          | `!SRCNAME(n,Name)`                                     |
| `!SRCENABLED?`    | Enabled input sources           | `!SRCENABLED(b)` — bitmask, bit0 = source 0            |
| `!VOI?`           | Selected voicing                | `!VOI(n)` — see Appendix B                             |
| `!VOINAME(n)?`    | Name of voicing n               | `!VOINAME(n,Name)`                                     |
| `!VOIENABLED?`    | Enabled voicings                | `!VOIENABLED(b)` — 16-bit mask, bit0 = Neutral         |
| `!RP?`            | RoomPerfect position            | `!RP(n)` — 0=Bypass, 1-8=Focus, 9=Global               |
| `!RPSTATUS?`      | RoomPerfect filter status       | `!RPSTATUS(b)` — 8-bit mask, bit0 = Focus1 present     |

## Commands

| Command          | Action                                                        |
| ---------------- | ------------------------------------------------------------- |
| `!OFF`           | Turn the amplifier off                                        |
| `!ON`            | Turn the amplifier on                                         |
| `!PWR`           | Toggle power (same as front standby button)                   |
| `!VOLDN`         | Decrease volume 1 step (0.5 dB)                               |
| `!VOLUP`         | Increase volume 1 step (0.5 dB)                               |
| `!VOLCH(d)`      | Change volume by delta d in 0.1 dB steps, e.g. `!VOLCH(-32)` = -3.2 dB |
| `!VOL(n)`        | Set volume to n (-999..120), 0.1 dB steps                     |
| `!MUTEON`        | Mute                                                          |
| `!MUTEOFF`       | Unmute                                                        |
| `!MUTE`          | Toggle mute                                                   |
| `!SRCDN`         | Previous enabled input source                                 |
| `!SRCUP`         | Next enabled input source                                     |
| `!SRC(n)`        | Select source n if enabled (see Appendix A)                   |
| `!SRCALL(n)`     | Select source n even if not enabled                           |
| `!RPDN`          | Previous RoomPerfect position                                 |
| `!RPUP`          | Next RoomPerfect position                                     |
| `!RPBP`          | RoomPerfect Bypass (if enabled)                               |
| `!RPFOC(n)`      | RoomPerfect Focus position n (1-8)                            |
| `!RPGLOB`        | RoomPerfect Global position                                   |
| `!VOIDN`         | Previous voicing                                              |
| `!VOIUP`         | Next voicing                                                  |
| `!VOI(n)`        | Select voicing n (see Appendix B)                             |
| `!SUBSCRIBE`     | Subscribe to status changes (source, RoomPerfect, voicing, power, mute) |
| `!UNSUBSCRIBE`   | Stop status subscription                                      |
| `!SUBSCRIBEVOL`  | Subscribe to volume changes                                   |
| `!UNSUBSCRIBEVOL`| Stop volume subscription                                      |

Subscription mode stays active until power is removed or the matching
unsubscribe command is received.

## Undocumented findings (not in the official manual)

Not in the official *External Control Manual* — found by protocol sniffing,
reverse-engineering the official Android app, and live testing against
actual hardware. See `src/lyngdorf.cpp` for the full derivation notes.

### `!AUDIOSTATUS(bitDepthCode,sampleRateCode,peakDb)` — async status

Pushed automatically whenever a new peak (or a format change) occurs
(`!AUDIOSTATUS?` also works as a request). `peakDb` is 0.1 dB units, -999 =
silence — **but this is a peak-hold value, not a live level.** Confirmed
2026-08-15 by watching it during playback: it only ever rises (tracking the
loudest moment seen), never falls back down through quiet passages, and only
resets to -999 on an amp power cycle (off then on). Active polling doesn't
get anything fresher either — between actual new peaks, the amp just returns
the same held value. No live/instantaneous level or VU-meter-style attribute
is known to exist on this protocol; a few plausible undocumented request
names (`!LEVEL?`, `!METER?`, `!PEAK?`, `!VU?`, etc.) were tried live and none
replied.

**Bit depth codes:**

| Code | Meaning | Code | Meaning |
| ---- | ------- | ---- | ------- |
| 1    | PCM     | 6-10 | DSD     |
| 2    | 16-bit  | 19   | PCM ADC |
| 3    | DSD     |      |         |
| 4    | 24-bit  |      |         |
| 5    | 32-bit  |      |         |

**Sample rate codes:**

| Code | Meaning   | Code | Meaning                                      |
| ---- | --------- | ---- | --------------------------------------------- |
| 4    | 22.05 kHz | 14   | 176.4 kHz                                     |
| 5    | 32 kHz    | 15   | 192 kHz                                       |
| 7    | 44.1 kHz  | 16   | DSD64                                         |
| 9    | 48 kHz    | 17   | DSD128 (also seen for out-of-spec 256x/512x)  |
| 12   | 88.2 kHz  |      |                                                |
| 13   | 96 kHz    |      |                                                |

Confirmed live against this TDAI-2170 unit for every code above. Codes
1/3/6-10/19 and 16/17 diverge from the official Android app's generic
lookup table (shared across Lyngdorf/Steinway's whole product line) — this
unit's own live behavior was trusted over the app's table where they disagreed.

### Home Cinema (fixed volume) detection on Analog 1

Analog 1 is the only input that supports Lyngdorf's "Home Cinema" mode — a
fixed/passthrough volume setting for using the TDAI-2170 as a power amp fed
by an AVR's pre-out. There's no documented status command for whether it's
enabled. The firmware detects it by probing: on switching to Analog 1, it
sends `!VOLUP` and checks whether the amp replies with an updated `!VOL(...)`
within 400 ms. No reply means fixed volume; a reply is immediately reverted
with `!VOLDN` so the probe has no lasting side effect.

## Appendix A: Input source numbering

| # | Source                          | # | Source                            |
| - | ------------------------------- | - | --------------------------------- |
| 0 | Coax Digital 1                  | 9 | HDMI Input 3                      |
| 1 | Coax Digital 2                  | 10| HDMI Input 4                      |
| 2 | Optical Digital 3               | 11| HDMI Audio Return Channel (ARC)   |
| 3 | Optical Digital 4               | 12| Analog 1 (RCA, main board)        |
| 4 | Optical Digital 5               | 13| Analog 2 (RCA, main board)        |
| 5 | Optical Digital 6               | 14| Analog 3 (RCA, extension board)   |
| 6 | USB Input                       | 15| Analog 4 (RCA, extension board)   |
| 7 | HDMI Input 1                    | 16| Analog 5 (RCA, extension board)   |
| 8 | HDMI Input 2                    | 17| Analog 6 (XLR, extension board)   |

## Appendix B: Voicing numbering

| # | Voicing | # | Voicing      |
| - | ------- | - | ------------ |
| 0 | Neutral | 7 | Action 1     |
| 1 | Music 1 | 8 | Action 2     |
| 2 | Music 2 | 9 | Movie        |
| 3 | Relaxed | 10| Action Movie |
| 4 | Open    | 11| News         |
| 5 | Open Air| 12| Bass 1       |
| 6 | Soft    | 13| Bass 2       |

## Appendix C: IR codes (NEC1)

Selected IR remote codes (NEC1 format), for reference:

| Command | NEC1 code        | Command       | NEC1 code        |
| ------- | ---------------- | ------------- | ---------------- |
| ON      | `0x10EF 0x807F`  | STANDBY       | `0x10EF 0x0FF0`  |
| OFF     | `0x10EF 0x817E`  | MUTE          | `0x10EF 0x13EC`  |
| VOL_UP  | `0x10EF 0x1AE5`  | VOL_DOWN      | `0x10EF 0x10EF`  |
| MENU    | `0x10EF 0x16E9`  | ENTER         | `0x10EF 0x19E6`  |

(Full IR code table is in the original manual.)
