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
