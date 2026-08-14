#include "lyngdorf.h"
#include "config.h"

LyngdorfState lyngState;

static HardwareSerial LyngSerial(LYNGDORF_UART_NUM);
static LyngRawSink  rawSink   = nullptr;
static LyngStateCb  stateCb   = nullptr;
static String       lineBuf;

// ---- protocol log ring buffer (for the debug web UI) --------------------
#define LYNG_LOG_LINES 40
static String  logBuf[LYNG_LOG_LINES];
static int     logHead  = 0;   // index of next write
static int     logCount = 0;

static void lyngLogAdd(const String& s) {
  logBuf[logHead] = "[" + String(millis() / 1000) + "] " + s;
  logHead = (logHead + 1) % LYNG_LOG_LINES;
  if (logCount < LYNG_LOG_LINES) logCount++;
}

int lyngLogSize() { return logCount; }

String lyngLogLine(int idx) {
  if (idx < 0 || idx >= logCount) return "";
  int start = (logHead - logCount + LYNG_LOG_LINES) % LYNG_LOG_LINES;
  return logBuf[(start + idx) % LYNG_LOG_LINES];
}

// Appendix A: input source numbering (index 0..17).
static const char* SOURCE_NAMES[] = {
  "Coax Digital 1", "Coax Digital 2", "Optical Digital 3", "Optical Digital 4",
  "Optical Digital 5", "Optical Digital 6", "USB Input", "HDMI Input 1",
  "HDMI Input 2", "HDMI Input 3", "HDMI Input 4", "HDMI ARC",
  "Analog 1", "Analog 2", "Analog 3", "Analog 4", "Analog 5", "Analog 6 (XLR)"
};
static const int SOURCE_COUNT = sizeof(SOURCE_NAMES) / sizeof(SOURCE_NAMES[0]);

// Appendix B: voicing numbering (index 0..13).
static const char* VOICING_NAMES[] = {
  "Neutral", "Music 1", "Music 2", "Relaxed", "Open", "Open Air", "Soft",
  "Action 1", "Action 2", "Movie", "Action Movie", "News", "Bass 1", "Bass 2"
};
static const int VOICING_COUNT = sizeof(VOICING_NAMES) / sizeof(VOICING_NAMES[0]);

const char* lyngSourceName(int n) {
  return (n >= 0 && n < SOURCE_COUNT) ? SOURCE_NAMES[n] : "";
}
const char* lyngVoicingName(int n) {
  return (n >= 0 && n < VOICING_COUNT) ? VOICING_NAMES[n] : "";
}
int lyngSourceIndexByName(const String& name) {
  for (int i = 0; i < SOURCE_COUNT; i++)
    if (name.equalsIgnoreCase(SOURCE_NAMES[i])) return i;
  return -1;
}
int lyngVoicingIndexByName(const String& name) {
  for (int i = 0; i < VOICING_COUNT; i++)
    if (name.equalsIgnoreCase(VOICING_NAMES[i])) return i;
  return -1;
}
float lyngVolumeDb() { return lyngState.volume / 10.0f; }

// !AUDIOSTATUS(bitDepthCode, sampleRateCode, levelDb) is not in Lyngdorf's
// published External Control Manual — originally reverse-engineered 2026-08
// by playing known-format tracks and watching the raw values settle after
// each source change (confirmed independent of physical input, seen
// identically over USB and HDMI, and of each other: bit depth and sample
// rate vary independently). Whatever arrives at the amp's own DSP chain is
// what's reported — a lossy source (e.g. web radio) reports whatever PCM
// rate/depth it was decoded to upstream, not its original compressed
// bitrate.
//
// Cross-checked 2026-08-14 against the official "My Lyngdorf" Android app
// (dk.slaudio.lyngdorf, decompiled for interoperability) — its kw3/hw3
// enums and the rb4.t/rb4.u dispatch in its status handler use the exact
// same two codes from the same 3-value comma split, confirming and
// extending the by-ear results below EXCEPT for DSD: the app's table (a
// generic one shared across Lyngdorf/Steinway's whole product line — TDAI,
// MP-4x/5x/6x, CD-2) says bitDepthCode 6-10 is DSD and leaves sampleRateCode
// unmapped for it. On this actual TDAI-2170, confirmed live 2026-08-14 by
// switching Volumio to "DSD Direct" (native, not DoP) and watching both the
// official app (which showed "DSD 2.82 MHz 1 bit" — 2.8224 MHz = DSD64) and
// this device's own /api/state simultaneously: it's bitDepthCode 3 +
// sampleRateCode 16, matching the original by-ear result, not the app's
// generic table. Every other code below (2, 4, 7, 9, 13, 15) was confirmed
// by both methods agreeing, so this unit's firmware apparently doesn't use
// the app's full generic range at all — it was never worth trusting over a
// direct, controlled test on the actual hardware in the first place.
//
// sampleRateCode 17 was found the same session by switching Volumio to
// DSD256 (11.28 MHz, confirmed via the file's own DSD-rate tag): paired
// with bitDepthCode 1 ("PCM") rather than 3, suggesting the amp's own DSD
// detection stops cleanly identifying bit depth above DSD64. A follow-up
// test playing a confirmed DSD128 (5.64 MHz) file produced the identical
// pair (bitDepthCode 1, sampleRateCode 17) — so 17 isn't a per-tier code,
// it's a single bucket that doesn't distinguish 128x from 256x. A DSD512
// test (audibly stuttering — the amp can't actually keep up with it) also
// read back as this same pair. Labeled "DSD128" rather than something more
// hedged because the TDAI-2170's Streaming USB Input Module is officially
// spec'd for "<=DSD128" — that's the rate this code is actually meant to
// represent; 256x/512x reaching the same code is the amp being fed
// something already out of spec, not a second valid tier.
static const char* audioSampleRateName(int code) {
  switch (code) {
    case 4:  return "22.05 kHz";
    case 5:  return "32 kHz";
    case 7:  return "44.1 kHz";
    case 9:  return "48 kHz";
    case 12: return "88.2 kHz";
    case 13: return "96 kHz";
    case 14: return "176.4 kHz";
    case 15: return "192 kHz";
    case 16: return "DSD64";
    case 17: return "DSD128";  // amp's official DSD ceiling; also seen for out-of-spec 256x/512x, see comment above
    default: return nullptr;
  }
}
static const char* audioBitDepthName(int code) {
  switch (code) {
    case 1:  return "PCM";
    case 2:  return "16-bit";
    case 3:  return "DSD";
    case 4:  return "24-bit";
    case 5:  return "32-bit";
    case 6: case 7: case 8: case 9: case 10: return "DSD";
    case 19: return "PCM ADC";
    default: return nullptr;
  }
}
String lyngAudioSampleRateName(int code) {
  const char* n = audioSampleRateName(code);
  return n ? String(n) : "Unknown (" + String(code) + ")";
}
String lyngAudioBitDepthName(int code) {
  const char* n = audioBitDepthName(code);
  return n ? String(n) : "Unknown (" + String(code) + ")";
}
String lyngAudioFormatText(int bitDepthCode, int sampleRateCode) {
  // sampleRateCode 16/17 (DSD64/DSD128) is a complete, self-describing label
  // on its own — bitDepthCode is just the amp's generic "PCM" fallback in
  // that case, not real information, so pairing them ("PCM / DSD128") reads
  // as a contradiction rather than a format description.
  if (sampleRateCode == 16 || sampleRateCode == 17)
    return lyngAudioSampleRateName(sampleRateCode);
  return lyngAudioBitDepthName(bitDepthCode) + " / " + lyngAudioSampleRateName(sampleRateCode);
}

// RoomPerfect: 0=Bypass, 1-8=Focus N, 9=Global.
String lyngRoomPerfectName(int n) {
  if (n == 0) return "Bypass";
  if (n >= 1 && n <= 8) return "Focus " + String(n);
  if (n == 9) return "Global";
  return "";
}
int lyngRoomPerfectIndexByName(const String& name) {
  if (name.equalsIgnoreCase("Bypass")) return 0;
  if (name.equalsIgnoreCase("Global")) return 9;
  if (name.startsWith("Focus") || name.startsWith("focus")) {
    int n = name.substring(5).toInt();
    if (n >= 1 && n <= 8) return n;
  }
  return -1;
}
String lyngRoomPerfectCommand(int n) {
  if (n == 0) return "!RPBP";
  if (n >= 1 && n <= 8) return "!RPFOC(" + String(n) + ")";
  if (n == 9) return "!RPGLOB";
  return "";
}

void lyngdorfSetRawSink(LyngRawSink sink)   { rawSink = sink; }
void lyngdorfSetStateCallback(LyngStateCb cb) { stateCb = cb; }

void lyngdorfBegin() {
  LyngSerial.begin(LYNGDORF_BAUD, SERIAL_8N1, LYNGDORF_RX_PIN, LYNGDORF_TX_PIN);
  lineBuf.reserve(256);
  Serial.printf("[lyng] UART%d @ %d 8N1 (RX=%d TX=%d)\n",
                LYNGDORF_UART_NUM, LYNGDORF_BAUD, LYNGDORF_RX_PIN, LYNGDORF_TX_PIN);
}

void lyngdorfSend(const String& cmd) {
  LyngSerial.print(cmd);
  LyngSerial.print("\r\n");
  lyngLogAdd(">> " + cmd);
  Serial.printf("[lyng] >> %s\n", cmd.c_str());
}

void lyngdorfWriteRaw(const uint8_t* data, size_t len) {
  LyngSerial.write(data, len);
}

// Parse one complete "!KEY(value)" line and update state.
static void parseLine(String s) {
  s.trim();
  if (!s.startsWith("!")) return;
  if (s.endsWith("!")) s.remove(s.length() - 1);   // async status marker

  String key, val;
  int lp = s.indexOf('(');
  if (lp >= 0) {
    int rp = s.indexOf(')', lp);
    key = s.substring(1, lp);
    val = s.substring(lp + 1, rp < 0 ? s.length() : rp);
  } else {
    key = s.substring(1);
  }
  key.toUpperCase();

  bool changed = false;
  if (key == "PWR") {
    bool p = val.equalsIgnoreCase("ON");
    changed = !lyngState.powerKnown || lyngState.power != p;
    lyngState.power = p; lyngState.powerKnown = true;
  } else if (key == "MUTE") {
    bool m = val.equalsIgnoreCase("ON");
    changed = !lyngState.muteKnown || lyngState.mute != m;
    lyngState.mute = m; lyngState.muteKnown = true;
  } else if (key == "VOL") {
    int v = val.toInt();
    changed = !lyngState.volKnown || lyngState.volume != v;
    lyngState.volume = v; lyngState.volKnown = true;
  } else if (key == "SRC") {
    int n = val.toInt();
    changed = !lyngState.srcKnown || lyngState.source != n;
    lyngState.source = n; lyngState.srcKnown = true;
  } else if (key == "VOI") {
    int n = val.toInt();
    changed = !lyngState.voiKnown || lyngState.voicing != n;
    lyngState.voicing = n; lyngState.voiKnown = true;
  } else if (key == "RP") {
    int n = val.toInt();
    changed = !lyngState.rpKnown || lyngState.rp != n;
    lyngState.rp = n; lyngState.rpKnown = true;
  } else if (key == "VER") {
    lyngState.version = val;
  } else if (key == "DEVICE") {
    lyngState.device = val;
  } else if (key == "AUDIOSTATUS") {
    int c1 = val.indexOf(',');
    int c2 = val.indexOf(',', c1 + 1);
    if (c1 > 0 && c2 > c1) {
      int bitDepthCode   = val.substring(0, c1).toInt();
      int sampleRateCode = val.substring(c1 + 1, c2).toInt();
      int levelDb        = val.substring(c2 + 1).toInt();
      // Only a format change (not the level, which updates many times/sec)
      // counts as "changed" — the level is polled via /api/state, not pushed.
      changed = !lyngState.audioKnown ||
                lyngState.audioBitDepthCode   != bitDepthCode ||
                lyngState.audioSampleRateCode != sampleRateCode;
      lyngState.audioBitDepthCode   = bitDepthCode;
      lyngState.audioSampleRateCode = sampleRateCode;
      lyngState.audioLevelDb        = levelDb;
      lyngState.audioKnown          = true;
    }
  }

  if (changed && stateCb) stateCb();
}

void lyngdorfLoop() {
  while (LyngSerial.available()) {
    uint8_t b = LyngSerial.read();
    if (rawSink) rawSink(&b, 1);          // transparent forward to TCP client(s)

    if (b == '\r' || b == '\n') {
      if (lineBuf.length()) {
        lyngLogAdd("<< " + lineBuf);
        Serial.printf("[lyng] << %s\n", lineBuf.c_str());   // echo amp replies to USB
        parseLine(lineBuf);
        lineBuf = "";
      }
    } else {
      lineBuf += (char)b;
      if (lineBuf.length() > 250) lineBuf = "";  // overflow guard
    }
  }
}
