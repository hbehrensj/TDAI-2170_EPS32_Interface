#include "debug_web.h"
#include "config.h"
#include "lyngdorf.h"
#include "mqtt_ha.h"
#include "net_config.h"
#include "selfupdate.h"
#include "tcp_bridge.h"
#include "diag.h"
#include <WiFi.h>
#include <ArduinoJson.h>

#if ENABLE_DEBUG_WEB

static const char PAGE_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TDAI-2170 debug</title>
<style>
  body{font-family:system-ui,sans-serif;margin:0;background:#0f1115;color:#e6e6e6}
  header{padding:12px 16px;background:#171a21;font-weight:600}
  .wrap{padding:16px;max-width:760px;margin:0 auto}
  .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:8px}
  .card{background:#171a21;border:1px solid #262b35;border-radius:8px;padding:10px}
  .k{font-size:11px;color:#8a93a6;text-transform:uppercase}
  .v{font-size:18px;margin-top:2px}
  .ok{color:#4ade80}.bad{color:#f87171}
  #log{background:#0a0c10;border:1px solid #262b35;border-radius:8px;padding:10px;
       height:240px;overflow:auto;font-family:ui-monospace,monospace;font-size:12px;
       white-space:pre-wrap}
  .rx{color:#7dd3fc}.tx{color:#fbbf24}
  button{background:#2563eb;color:#fff;border:0;border-radius:6px;padding:8px 12px;
         margin:2px;cursor:pointer;font-size:13px}
  button.sec{background:#374151}
  input{background:#0a0c10;border:1px solid #262b35;color:#e6e6e6;border-radius:6px;
        padding:8px;width:60%;font-family:ui-monospace,monospace}
  input[type=range]{width:100%;padding:0}
  h3{margin:18px 0 8px}
  button.active{background:#16a34a}
</style></head><body>
<header>Lyngdorf TDAI-2170 &mdash; debug</header>
<div class="wrap">
  <h3>Status</h3>
  <div class="grid" id="status"></div>
  <h3>Quick commands</h3>
  <div>
    <button onclick="cmd('!PWR')">Power</button>
    <button onclick="cmd('!MUTE')">Mute</button>
    <button class="sec" onclick="cmd('!VOLDN')">Vol &minus;</button>
    <button class="sec" onclick="cmd('!VOLUP')">Vol +</button>
    <button class="sec" onclick="cmd('!SRCDN')">Src &lt;</button>
    <button class="sec" onclick="cmd('!SRCUP')">Src &gt;</button>
    <button class="sec" onclick="cmd('!VOIDN')">Voi &lt;</button>
    <button class="sec" onclick="cmd('!VOIUP')">Voi &gt;</button>
    <button class="sec" onclick="cmd('!RPDN')">RP &lt;</button>
    <button class="sec" onclick="cmd('!RPUP')">RP &gt;</button>
  </div>
  <div style="margin-top:8px">
    <input id="raw" placeholder="!VOL(-200)" onkeydown="if(event.key=='Enter')sendRaw()">
    <button onclick="sendRaw()">Send</button>
  </div>
  <h3>Volume</h3>
  <div class="card">
    <input type="range" id="volslider" min="-60" max="12" step="0.5"
           oninput="volLabel()" onchange="volSet()">
    <div class="k" id="vollabel" style="margin-top:6px"></div>
  </div>
  <h3>Sources</h3>
  <div class="grid" id="sources"></div>
  <h3>UART log</h3>
  <div id="log"></div>

  <h3>MQTT settings</h3>
  <div class="card">
    <div style="display:grid;grid-template-columns:90px 1fr;gap:8px;align-items:center">
      <label>Host</label><input id="mqtt_host" placeholder="192.168.1.x">
      <label>Port</label><input id="mqtt_port" placeholder="1883">
      <label>User</label><input id="mqtt_user" placeholder="(optional)">
      <label>Password</label><input id="mqtt_pass" type="password" placeholder="(unchanged if blank)">
    </div>
    <div style="margin-top:10px">
      <button onclick="saveMqtt()">Save MQTT</button>
      <span id="mqtt_msg" style="margin-left:10px;font-size:13px"></span>
    </div>
  </div>

  <h3>Firmware update</h3>
  <div class="card">
    <button onclick="checkUpdate()">Check GitHub for update</button>
    <span id="upd_msg" style="margin-left:10px;font-size:13px"></span>
    <div class="k" style="margin-top:8px">Pulls the latest release; reboots if a newer version is found.</div>
  </div>
</div>
<script>
function cmd(c){fetch('/api/cmd?c='+encodeURIComponent(c))}
function sendRaw(){var e=document.getElementById('raw');if(e.value){cmd(e.value);e.value=''}}
function badge(b){return b?'<span class=ok>yes</span>':'<span class=bad>no</span>'}

// Appendix A source order (see SOURCE_NAMES in lyngdorf.cpp) — index is the !SRC(n) value.
const SOURCES=["Coax Digital 1","Coax Digital 2","Optical Digital 3","Optical Digital 4",
  "Optical Digital 5","Optical Digital 6","USB Input","HDMI Input 1","HDMI Input 2",
  "HDMI Input 3","HDMI Input 4","HDMI ARC","Analog 1","Analog 2","Analog 3","Analog 4",
  "Analog 5","Analog 6 (XLR)"];
function srcCmd(i){cmd('!SRC('+i+')')}
function initSources(){
  document.getElementById('sources').innerHTML=SOURCES.map((n,i)=>
    '<button class="sec" id="src'+i+'" onclick="srcCmd('+i+')">'+n+'</button>').join('');
}
let volslider,vollabel,volDragging=false;
function volLabel(){vollabel.textContent=parseFloat(volslider.value).toFixed(1)+' dB'}
function volSet(){cmd('!VOL('+Math.round(parseFloat(volslider.value)*10)+')')}
async function tick(){
  try{
    let s=await (await fetch('/api/state')).json();
    document.getElementById('status').innerHTML=
      card('WiFi',s.wifi.ssid+' ('+s.wifi.rssi+' dBm)')+
      card('IP',s.wifi.ip)+
      card('Device FW',s.fw)+
      card('MQTT',badge(s.mqtt))+
      card('TCP clients',s.tcp_clients)+
      card('Power',s.amp.power)+
      card('Mute',s.amp.mute)+
      card('Volume',s.amp.volume_db+' dB')+
      card('Source',s.amp.source)+
      card('Voicing',s.amp.voicing)+
      card('RoomPerfect',s.amp.roomperfect)+
      card('Amp FW',s.amp.version)+
      card('Amp model',s.amp.device)+
      card('Audio format',s.amp.audio_format)+
      card('Audio level',s.amp.audio_level_db)+
      card('Uptime',fmtUp(s.uptime_s))+
      card('Last reboot',s.last_reset)+
      card('Last WiFi drop',s.last_wifi_drop);
    if(!volDragging && s.amp.volume_db!=='?'){volslider.value=s.amp.volume_db;volLabel()}
    for(let i=0;i<SOURCES.length;i++){
      let b=document.getElementById('src'+i);
      if(b)b.classList.toggle('active',s.amp.source===SOURCES[i]);
    }
    let lines=await (await fetch('/api/log')).json();
    let el=document.getElementById('log');
    let atBottom=el.scrollTop+el.clientHeight>=el.scrollHeight-10;
    el.innerHTML=lines.map(l=>{
      let c=l.includes('>> ')?'tx':l.includes('<< ')?'rx':'';
      return '<div class="'+c+'">'+l.replace(/</g,'&lt;')+'</div>';
    }).join('');
    if(atBottom)el.scrollTop=el.scrollHeight;
  }catch(e){}
}
function card(k,v){return '<div class=card><div class=k>'+k+'</div><div class=v>'+v+'</div></div>'}
function fmtUp(s){var d=s/86400|0,h=s%86400/3600|0,m=s%3600/60|0;
  return d?d+'d '+h+'h':h?h+'h '+m+'m':m+'m'}
async function loadMqtt(){
  try{
    let m=await (await fetch('/api/mqtt')).json();
    mqtt_host.value=m.host; mqtt_port.value=m.port; mqtt_user.value=m.user;
    mqtt_pass.placeholder=m.has_pass?'(unchanged if blank)':'(none set)';
  }catch(e){}
}
async function saveMqtt(){
  let b=new URLSearchParams({host:mqtt_host.value,port:mqtt_port.value,
    user:mqtt_user.value,pass:mqtt_pass.value});
  let el=document.getElementById('mqtt_msg');
  try{
    let r=await fetch('/api/mqtt',{method:'POST',
      headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});
    el.textContent=r.ok?'saved ✓ (reconnecting)':'error';
    el.className=r.ok?'ok':'bad'; mqtt_pass.value='';
  }catch(e){el.textContent='error';el.className='bad';}
}
async function checkUpdate(){
  let el=document.getElementById('upd_msg');
  el.textContent='checking…'; el.className='';
  try{
    let r=await fetch('/api/update',{method:'POST'});
    el.textContent=await r.text();
  }catch(e){el.textContent='(rebooting if updating…)';}
}
volslider=document.getElementById('volslider');vollabel=document.getElementById('vollabel');
volslider.addEventListener('pointerdown',()=>volDragging=true);
volslider.addEventListener('pointerup',()=>volDragging=false);
initSources();
setInterval(tick,1000);tick();loadMqtt();
</script></body></html>)HTML";

static void handleRoot(WebServer* s) {
  s->send_P(200, "text/html", PAGE_HTML);
}

static void handleState(WebServer* s) {
  JsonDocument d;
  d["wifi"]["ssid"]    = WiFi.SSID();
  d["wifi"]["ip"]      = WiFi.localIP().toString();
  d["wifi"]["rssi"]    = WiFi.RSSI();
  d["wifi"]["gateway"] = WiFi.gatewayIP().toString();
  d["wifi"]["subnet"]  = WiFi.subnetMask().toString();
  d["wifi"]["dns"]     = WiFi.dnsIP().toString();
  d["mqtt"]         = mqttConnected();
  d["tcp_clients"]  = tcpBridgeClientCount();
  d["fw"]           = FIRMWARE_VERSION_STR;
  d["last_reset"]   = diagLastResetText();
  d["last_wifi_drop"] = diagLastWifiDropText();
  d["uptime_s"]     = diagUptimeSeconds();

  JsonObject a = d["amp"].to<JsonObject>();
  a["power"]   = lyngState.powerKnown ? (lyngState.power ? "on" : "off") : "?";
  a["mute"]    = lyngState.muteKnown  ? (lyngState.mute ? "on" : "off")  : "?";
  a["volume_db"] = lyngState.volKnown ? String(lyngVolumeDb(), 1) : "?";
  a["source"]  = lyngState.srcKnown ? lyngSourceName(lyngState.source) : "?";
  a["voicing"] = lyngState.voiKnown ? lyngVoicingName(lyngState.voicing) : "?";
  a["roomperfect"] = lyngState.rpKnown ? lyngRoomPerfectName(lyngState.rp) : "?";
  a["version"] = lyngState.version.length() ? lyngState.version : "?";
  a["device"]  = lyngState.device.length()  ? lyngState.device  : "?";
  a["audio_format"] = lyngState.audioKnown
      ? lyngAudioFormatText(lyngState.audioBitDepthCode, lyngState.audioSampleRateCode)
      : "?";
  a["audio_level_db"] = lyngState.audioKnown
      ? (lyngState.audioLevelDb <= -999 ? "silence" : String(lyngState.audioLevelDb / 10.0f, 1) + " dB")
      : "?";

  String out; serializeJson(d, out);
  s->send(200, "application/json", out);
}

static void handleLog(WebServer* s) {
  JsonDocument d;
  JsonArray arr = d.to<JsonArray>();
  int n = lyngLogSize();
  for (int i = 0; i < n; i++) arr.add(lyngLogLine(i));
  String out; serializeJson(d, out);
  s->send(200, "application/json", out);
}

static void handleCmd(WebServer* s) {
  String c = s->arg("c");
  if (c.length()) lyngdorfSend(c);
  s->send(200, "text/plain", "ok");
}

// Active TCP reachability test from the device itself:
//   /api/nettest?host=192.168.1.80&port=1883
// Tells us whether the ESP32 (not the browser) can open a socket.
static void handleNetTest(WebServer* s) {
  String host = s->arg("host");
  int    port = s->arg("port").toInt();
  if (host.isEmpty() || port <= 0) { s->send(400, "text/plain", "need host&port"); return; }
  WiFiClient c;
  uint32_t t0 = millis();
  bool ok = c.connect(host.c_str(), (uint16_t)port, 4000);
  uint32_t ms = millis() - t0;
  c.stop();
  JsonDocument d;
  d["host"] = host; d["port"] = port; d["connected"] = ok; d["ms"] = ms;
  String out; serializeJson(d, out);
  s->send(200, "application/json", out);
}

static void handleMqttGet(WebServer* s) {
  JsonDocument d;
  d["host"]     = netMqttHost();
  d["port"]     = netMqttPort();
  d["user"]     = netMqttUser();
  d["has_pass"] = !netMqttPass().isEmpty();   // never echo the password back
  String out; serializeJson(d, out);
  s->send(200, "application/json", out);
}

static void handleMqttSet(WebServer* s) {
  netSetMqtt(s->arg("host"), s->arg("port"), s->arg("user"), s->arg("pass"));
  s->send(200, "text/plain", "ok");
}

static void handleUpdate(WebServer* s) {
  // Blocks while checking/downloading; reboots on success (response may not
  // arrive in that case, which is expected).
  s->send(200, "text/plain", selfUpdateCheckNow());
}

void debugWebRegister(WebServer& server) {
  server.on("/", HTTP_GET, [&server]() { handleRoot(&server); });
  server.on("/api/state", HTTP_GET, [&server]() { handleState(&server); });
  server.on("/api/log", HTTP_GET, [&server]() { handleLog(&server); });
  server.on("/api/cmd", HTTP_GET, [&server]() { handleCmd(&server); });
  server.on("/api/nettest", HTTP_GET, [&server]() { handleNetTest(&server); });
  server.on("/api/mqtt", HTTP_GET, [&server]() { handleMqttGet(&server); });
  server.on("/api/mqtt", HTTP_POST, [&server]() { handleMqttSet(&server); });
  server.on("/api/update", HTTP_POST, [&server]() { handleUpdate(&server); });
}

#endif  // ENABLE_DEBUG_WEB
