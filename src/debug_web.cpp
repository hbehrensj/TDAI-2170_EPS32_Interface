#include "debug_web.h"
#include "config.h"
#include "lyngdorf.h"
#include "mqtt_ha.h"
#include "tcp_bridge.h"
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
  h3{margin:18px 0 8px}
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
  </div>
  <div style="margin-top:8px">
    <input id="raw" placeholder="!VOL(-200)" onkeydown="if(event.key=='Enter')sendRaw()">
    <button onclick="sendRaw()">Send</button>
  </div>
  <h3>UART log</h3>
  <div id="log"></div>
</div>
<script>
function cmd(c){fetch('/api/cmd?c='+encodeURIComponent(c))}
function sendRaw(){var e=document.getElementById('raw');if(e.value){cmd(e.value);e.value=''}}
function badge(b){return b?'<span class=ok>yes</span>':'<span class=bad>no</span>'}
async function tick(){
  try{
    let s=await (await fetch('/api/state')).json();
    document.getElementById('status').innerHTML=
      card('WiFi',s.wifi.ssid+' ('+s.wifi.rssi+' dBm)')+
      card('IP',s.wifi.ip)+
      card('MQTT',badge(s.mqtt))+
      card('TCP clients',s.tcp_clients)+
      card('Power',s.amp.power)+
      card('Mute',s.amp.mute)+
      card('Volume',s.amp.volume_db+' dB')+
      card('Source',s.amp.source)+
      card('Voicing',s.amp.voicing);
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
setInterval(tick,1000);tick();
</script></body></html>)HTML";

static void handleRoot(WebServer* s) {
  s->send_P(200, "text/html", PAGE_HTML);
}

static void handleState(WebServer* s) {
  JsonDocument d;
  d["wifi"]["ssid"] = WiFi.SSID();
  d["wifi"]["ip"]   = WiFi.localIP().toString();
  d["wifi"]["rssi"] = WiFi.RSSI();
  d["mqtt"]         = mqttConnected();
  d["tcp_clients"]  = tcpBridgeClientCount();

  JsonObject a = d["amp"].to<JsonObject>();
  a["power"]   = lyngState.powerKnown ? (lyngState.power ? "on" : "off") : "?";
  a["mute"]    = lyngState.muteKnown  ? (lyngState.mute ? "on" : "off")  : "?";
  a["volume_db"] = lyngState.volKnown ? String(lyngVolumeDb(), 1) : "?";
  a["source"]  = lyngState.srcKnown ? lyngSourceName(lyngState.source) : "?";
  a["voicing"] = lyngState.voiKnown ? lyngVoicingName(lyngState.voicing) : "?";

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

void debugWebRegister(WebServer& server) {
  server.on("/", HTTP_GET, [&server]() { handleRoot(&server); });
  server.on("/api/state", HTTP_GET, [&server]() { handleState(&server); });
  server.on("/api/log", HTTP_GET, [&server]() { handleLog(&server); });
  server.on("/api/cmd", HTTP_GET, [&server]() { handleCmd(&server); });
}

#endif  // ENABLE_DEBUG_WEB
