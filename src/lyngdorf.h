#pragma once
#include <Arduino.h>

// Lyngdorf TDAI-2170 serial protocol (115200 8N1, ASCII, '!'-prefixed,
// <CR><LF>-terminated). See docs/serial-protocol.md for the full command set.

struct LyngdorfState {
  bool powerKnown = false;  bool power = false;
  bool muteKnown  = false;  bool mute  = false;
  bool volKnown   = false;  int  volume = 0;   // raw value, 0.1 dB units (-999..120)
  bool srcKnown   = false;  int  source = -1;  // 0..17, see Appendix A
  bool voiKnown   = false;  int  voicing = -1; // 0..13, see Appendix B
  bool rpKnown    = false;  int  rp = -1;      // RoomPerfect: 0=Bypass,1-8=Focus,9=Global
  String version;                              // !VER reply
  String device;                              // !DEVICE reply

  // !AUDIOSTATUS(bitDepthCode, sampleRateCode, levelDb) — undocumented async
  // status (not in the official manual); see lyngAudioSampleRateName/
  // lyngAudioBitDepthName. levelDb is 0.1 dB units, -999 = silence.
  //
  // NOT a live/instantaneous level — confirmed 2026-08-15 by watching it
  // live while music played: it only ever rises (tracking the loudest
  // moment seen), never falls back down even through quiet passages, and
  // only resets to -999 on an amp power cycle (off then on). It's a peak
  // hold since power-on, not a VU/RMS meter. That also explains the
  // irregular push timing measured the same day (195ms-4.3s between
  // updates, median ~430ms) — the amp only pushes a new one when a new
  // peak is actually set, not on a fixed clock; active polling faster than
  // that just re-reads the same held value. Named audioPeakDb accordingly.
  // Deliberately does not trigger the state-changed callback on its own
  // (only a bit-depth/sample-rate change does) — a UI wanting it has to
  // poll /api/state itself.
  bool audioKnown = false;
  int  audioBitDepthCode   = -1;
  int  audioSampleRateCode = -1;
  int  audioPeakDb         = -999;

  // Whether the currently selected Analog 1 input is configured for
  // Lyngdorf's "Home Cinema" mode (fixed/passthrough volume, for using this
  // amp as a power amp fed by an AVR's pre-out — Analog 1 is the only input
  // that supports it). Detected by probing on every switch to Analog 1: send
  // !VOLUP and see whether the amp replies with an updated !VOL(...) within
  // a short timeout (see lyngdorfLoop() in lyngdorf.cpp). Only meaningful
  // while source == Analog 1; reset to unknown on any other source.
  bool homeCinemaKnown = false;
  bool homeCinema      = false;
};

extern LyngdorfState lyngState;

// Raw byte sink: every byte received from the amp is also forwarded here
// (used by the transparent TCP bridge so the Lyngdorf app sees everything).
typedef void (*LyngRawSink)(const uint8_t* data, size_t len);

// Called whenever a parsed status value changes.
typedef void (*LyngStateCb)();

void lyngdorfBegin();
void lyngdorfLoop();

void lyngdorfSend(const String& cmd);                 // appends <CR><LF>
void lyngdorfWriteRaw(const uint8_t* data, size_t len); // verbatim (TCP -> UART)

void lyngdorfSetRawSink(LyngRawSink sink);
void lyngdorfSetStateCallback(LyngStateCb cb);

// Convenience helpers for the higher layers.
const char* lyngSourceName(int n);   // returns "" if out of range
const char* lyngVoicingName(int n);
int  lyngSourceIndexByName(const String& name);   // -1 if not found
int  lyngVoicingIndexByName(const String& name);  // -1 if not found
float lyngVolumeDb();                             // volume in dB

// RoomPerfect position 0=Bypass, 1-8=Focus N, 9=Global. Helpers map to/from
// the names used by Home Assistant select / MCP tools.
String lyngRoomPerfectName(int n);                // "" if out of range
int    lyngRoomPerfectIndexByName(const String& name);  // -1 if not found
String lyngRoomPerfectCommand(int n);             // serial command for position n

// AUDIOSTATUS format codes, reverse-engineered by observation and
// cross-checked against the official Android app (undocumented by Lyngdorf)
// — see the comment above audioSampleRateName() in lyngdorf.cpp for the
// known mappings and how they were derived. Returns "Unknown (n)" for any
// code not yet seen.
String lyngAudioSampleRateName(int code);
String lyngAudioBitDepthName(int code);

// Combines both into the text shown to users, collapsing the redundant/
// misleading "PCM / DSD128"-style pairing down to just the DSD label —
// see the comment above this function's definition in lyngdorf.cpp.
String lyngAudioFormatText(int bitDepthCode, int sampleRateCode);

// Recent protocol traffic (ring buffer) for the debug web UI.
int    lyngLogSize();          // number of retained lines
String lyngLogLine(int idx);   // idx 0 = oldest retained, size-1 = newest
