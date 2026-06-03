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
int  lyngSourceIndexByName(const String& name);  // -1 if not found
float lyngVolumeDb();                            // volume in dB

// Recent protocol traffic (ring buffer) for the debug web UI.
int    lyngLogSize();          // number of retained lines
String lyngLogLine(int idx);   // idx 0 = oldest retained, size-1 = newest
