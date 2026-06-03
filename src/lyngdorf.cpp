#include "lyngdorf.h"
#include "config.h"

LyngdorfState lyngState;

static HardwareSerial LyngSerial(LYNGDORF_UART_NUM);
static LyngRawSink  rawSink   = nullptr;
static LyngStateCb  stateCb   = nullptr;
static String       lineBuf;

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
float lyngVolumeDb() { return lyngState.volume / 10.0f; }

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
  }

  if (changed && stateCb) stateCb();
}

void lyngdorfLoop() {
  while (LyngSerial.available()) {
    uint8_t b = LyngSerial.read();
    if (rawSink) rawSink(&b, 1);          // transparent forward to TCP client(s)

    if (b == '\r' || b == '\n') {
      if (lineBuf.length()) { parseLine(lineBuf); lineBuf = ""; }
    } else {
      lineBuf += (char)b;
      if (lineBuf.length() > 250) lineBuf = "";  // overflow guard
    }
  }
}
