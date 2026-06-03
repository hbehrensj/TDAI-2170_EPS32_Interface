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
