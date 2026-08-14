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
  // lyngAudioBitDepthName. levelDb is 0.1 dB units, -999 = silence, updates
  // many times/sec so it deliberately does NOT trigger the state-changed
  // callback on its own (only a bit-depth/sample-rate change does).
  bool audioKnown = false;
  int  audioBitDepthCode   = -1;
  int  audioSampleRateCode = -1;
  int  audioLevelDb        = -999;
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
