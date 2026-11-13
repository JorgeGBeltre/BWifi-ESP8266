#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include "esp8266_compat.h"
#include "settings_menu.h"
#include "games_menu.h"

extern ESP8266WebServer webDashServer;
extern String apSSID;

static const char kWebDashHtml[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1.0'>
<title>BWifi-ESP8266 v3.5 Command Center</title>
<style>
  body { background:#0d1117; color:#c9d1d9; font-family:monospace; margin:0; padding:14px; }
  .container { max-width:720px; margin:auto; border:1px solid #30363d; padding:16px; border-radius:8px; background:#161b22; }
  h1 { color:#58a6ff; text-align:center; font-size:19px; border-bottom:1px solid #30363d; padding-bottom:10px; margin-top:0; }
  .stats { display:grid; grid-template-columns:repeat(2,1fr); gap:8px; margin-bottom:12px; font-size:12px; }
  .card { background:#0d1117; border:1px solid #30363d; border-radius:6px; padding:8px; }
  .tabs { display:flex; gap:5px; margin-bottom:10px; flex-wrap:wrap; }
  .tab { flex:1; padding:7px; background:#21262d; color:#c9d1d9; border:1px solid #30363d; border-radius:5px; cursor:pointer; text-align:center; font-weight:bold; font-size:11px; }
  .tab.active { background:#1f6feb; color:#fff; }
  .log-box { background:#010409; padding:12px; border-radius:6px; border:1px solid #21262d; white-space:pre-wrap; height:220px; overflow-y:auto; color:#39d353; font-size:12px; }
  textarea { width:100%; height:200px; background:#010409; color:#58a6ff; border:1px solid #30363d; border-radius:6px; padding:8px; font-family:monospace; box-sizing:border-box; display:none; }
  .btns { display:grid; grid-template-columns:repeat(2,1fr); gap:8px; margin-top:10px; }
  .btn { padding:9px; border:none; border-radius:6px; cursor:pointer; font-weight:bold; text-align:center; text-decoration:none; font-size:11px; color:#fff; }
  .btn-dl { background:#238636; }
  .btn-pcap { background:#8957e5; }
  .btn-bmp { background:#1f6feb; }
  .btn-del { background:#da3633; }
</style></head><body>
<div class='container'>
  <h1>BWIFI-ESP8266 v3.5 COMMAND CENTER</h1>
  <div class='stats'>
    <div class='card'>RAM Libre: <b id='st-heap'>...</b></div>
    <div class='card'>LittleFS: <b id='st-fs'>...</b></div>
    <div class='card'>Uptime: <b id='st-up'>...</b></div>
    <div class='card'>Perfil: <b id='st-prof'>...</b></div>
  </div>
  <div class='tabs'>
    <div class='tab active' id='t-logins' onclick="setTab('logins')">LOGINS</div>
    <div class='tab' id='t-wardrive' onclick="setTab('wardrive')">WARDRIVE</div>
    <div class='tab' id='t-trackers' onclick="setTab('trackers')">TRACKERS</div>
    <div class='tab' id='t-scores' onclick="setTab('scores')">LEADERBOARD</div>
    <div class='tab' id='t-ducky' onclick="setTab('ducky')">DUCKY SCRIPT</div>
  </div>
  <div id='log-content' class='log-box'>Cargando...</div>
  <textarea id='ducky-ed'></textarea>
  <div class='btns'>
    <a id='dl-link' href='/download-logs' class='btn btn-dl'>DESCARGAR LOG/CSV</a>
    <a href='/download-pcap' class='btn btn-pcap'>BAJAR .PCAP 802.11</a>
    <a href='/snap-bmp' class='btn btn-bmp'>CAPTURAR OLED</a>
    <a href='/download-screen' class='btn btn-bmp'>BAJAR BMP OLED</a>
    <button id='act-btn' onclick='clearLogs()' class='btn btn-del' style='grid-column:span 2;'>BORRAR LOGS</button>
  </div>
</div>
<script>
  let curTab = 'logins';
  function setTab(t) {
    curTab = t;
    ['logins','wardrive','trackers','scores','ducky'].forEach(k=>{
      document.getElementById('t-'+k).className = 'tab' + (t===k?' active':'');
    });
    const lb = document.getElementById('log-content');
    const ed = document.getElementById('ducky-ed');
    const act = document.getElementById('act-btn');
    if (t==='ducky') {
      lb.style.display='none'; ed.style.display='block';
      act.innerText='GUARDAR DUCKY'; act.style.background='#238636';
      act.onclick=saveDucky;
      fetch('/get-ducky').then(r=>r.text()).then(d=>{ ed.value=d; });
    } else {
      lb.style.display='block'; ed.style.display='none';
      act.innerText='BORRAR LOGS'; act.style.background='#da3633';
      act.onclick=clearLogs;
      document.getElementById('dl-link').href = (t==='logins')?'/download-logs':((t==='wardrive')?'/download-wardrive':'/download-trackers');
      fetchLogs();
    }
  }
  function saveDucky() {
    const val = document.getElementById('ducky-ed').value;
    fetch('/save-ducky?script='+encodeURIComponent(val)).then(()=>alert('Guardado en /payload.txt'));
  }
  function fetchStatus() {
    fetch('/api/status').then(r=>r.json()).then(d=>{
      document.getElementById('st-heap').innerText = d.heap + ' KB';
      document.getElementById('st-fs').innerText = d.fs_used + '/' + d.fs_total + ' KB';
      document.getElementById('st-up').innerText = d.uptime + ' s';
      document.getElementById('st-prof').innerText = d.profile;
    }).catch(()=>{});
  }
  function fetchLogs() {
    if (curTab==='ducky') return;
    let ep = '/get-logs';
    if (curTab==='wardrive') ep='/get-wardrive';
    else if (curTab==='trackers') ep='/get-trackers';
    else if (curTab==='scores') ep='/api/leaderboard';
    fetch(ep).then(r=>r.text()).then(d=>{
      document.getElementById('log-content').innerText = d || 'Sin registros.';
    });
  }
  function clearLogs() {
    if(confirm('¿Borrar todos los logs en LittleFS?')) {
      fetch('/clear-logs').then(()=>fetchLogs());
    }
  }
  setInterval(()=>{ fetchStatus(); fetchLogs(); }, 4000);
  window.onload = ()=>{ fetchStatus(); fetchLogs(); };
</script></body></html>
)rawliteral";

inline void sendSpiffsFile(const char *path, const char *mime, bool download, const char *dlName) {
  if (!SPIFFS.exists(path)) {
    webDashServer.send(200, "text/plain", "Sin registros en " + String(path));
    return;
  }
  File f = SPIFFS.open(path, "r");
  if (!f) {
    webDashServer.send(500, "text/plain", "Error abriendo archivo");
    return;
  }
  if (download) {
    webDashServer.sendHeader("Content-Disposition", String("attachment; filename=") + dlName);
  }
  webDashServer.streamFile(f, mime);
  f.close();
}

inline void startWebServer() {
  SPIFFS.begin();

  uint32_t chipID = ESP.getChipId();
  apSSID = "BW8266-" + String(chipID, HEX);
  apSSID.toUpperCase();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID.c_str(), "12345678");

  webDashServer.on("/", HTTP_GET, []() {
    webDashServer.send_P(200, "text/html", kWebDashHtml);
  });

  webDashServer.on("/api/status", HTTP_GET, []() {
    FSInfo fs_info;
    SPIFFS.info(fs_info);
    const char *profNames[] = {"LAB", "AUDIT", "DEMO"};
    String json = "{";
    json += "\"heap\":" + String(ESP.getFreeHeap() / 1024) + ",";
    json += "\"fs_used\":" + String(fs_info.usedBytes / 1024) + ",";
    json += "\"fs_total\":" + String(fs_info.totalBytes / 1024) + ",";
    json += "\"uptime\":" + String(millis() / 1000) + ",";
    json += "\"profile\":\"" + String(profNames[g_settings.profileMode % 3]) + "\"";
    json += "}";
    webDashServer.send(200, "application/json", json);
  });

  webDashServer.on("/api/leaderboard", HTTP_GET, []() {
    const char *names[10] = {
      "1. SNAKE", "2. PONG", "3. FLAPPY", "4. INVADERS", "5. DINO RUN",
      "6. BREAKOUT", "7. SIMON MEMORIA", "8. TETRIS", "9. ASTEROIDS", "10. 2048 PUZZLE"
    };
    String out = "=== ARCADE LEADERBOARD (EEPROM v3.5) ===\n\n";
    for (int i = 0; i < 10; i++) {
      out += String(names[i]) + "  --->  HI-SCORE: " + String(getArcadeHighScore(i)) + "\n";
    }
    webDashServer.send(200, "text/plain", out);
  });

  webDashServer.on("/get-scores", HTTP_GET, []() {
    const char *names[10] = {
      "1. SNAKE", "2. PONG", "3. FLAPPY", "4. INVADERS", "5. DINO RUN",
      "6. BREAKOUT", "7. SIMON MEMORIA", "8. TETRIS", "9. ASTEROIDS", "10. 2048 PUZZLE"
    };
    String out = "=== ARCADE LEADERBOARD (EEPROM v3.5) ===\n\n";
    for (int i = 0; i < 10; i++) {
      out += String(names[i]) + "  --->  HI-SCORE: " + String(getArcadeHighScore(i)) + "\n";
    }
    webDashServer.send(200, "text/plain", out);
  });

  webDashServer.on("/get-ducky", HTTP_GET, []() {
    sendSpiffsFile("/payload.txt", "text/plain", false, "payload.txt");
  });

  webDashServer.on("/save-ducky", HTTP_GET, []() {
    if (webDashServer.hasArg("script")) {
      File f = SPIFFS.open("/payload.txt", "w");
      if (f) {
        f.print(webDashServer.arg("script"));
        f.close();
      }
    }
    webDashServer.send(200, "text/plain", "OK");
  });

  webDashServer.on("/get-logs", HTTP_GET, []() {
    sendSpiffsFile("/logins.txt", "text/plain", false, "logins.txt");
  });
  webDashServer.on("/download-logs", HTTP_GET, []() {
    sendSpiffsFile("/logins.txt", "text/plain", true, "logins.txt");
  });
  webDashServer.on("/get-wardrive", HTTP_GET, []() {
    sendSpiffsFile("/wardrive.csv", "text/plain", false, "wardrive.csv");
  });
  webDashServer.on("/download-wardrive", HTTP_GET, []() {
    sendSpiffsFile("/wardrive.csv", "text/csv", true, "wardrive.csv");
  });
  webDashServer.on("/get-trackers", HTTP_GET, []() {
    sendSpiffsFile("/trackers.txt", "text/plain", false, "trackers.txt");
  });
  webDashServer.on("/download-trackers", HTTP_GET, []() {
    sendSpiffsFile("/trackers.txt", "text/plain", true, "trackers.txt");
  });
  webDashServer.on("/download-pcap", HTTP_GET, []() {
    sendSpiffsFile("/capture.pcap", "application/octet-stream", true, "capture.pcap");
  });
  webDashServer.on("/download-screen", HTTP_GET, []() {
    if (!SPIFFS.exists("/screen.bmp")) {
      saveOledScreenshotBmp("/screen.bmp");
    }
    sendSpiffsFile("/screen.bmp", "image/bmp", true, "screen.bmp");
  });
  webDashServer.on("/snap-bmp", HTTP_GET, []() {
    saveOledScreenshotBmp("/screen.bmp");
    sendSpiffsFile("/screen.bmp", "image/bmp", true, "screen.bmp");
  });

  webDashServer.on("/clear-logs", HTTP_GET, []() {
    SPIFFS.remove("/logins.txt");
    SPIFFS.remove("/logins.txt.1");
    SPIFFS.remove("/wardrive.csv");
    SPIFFS.remove("/wardrive.csv.1");
    SPIFFS.remove("/trackers.txt");
    SPIFFS.remove("/trackers.txt.1");
    SPIFFS.remove("/capture.pcap");
    SPIFFS.remove("/capture.pcap.1");
    webDashServer.send(200, "text/plain", "OK");
  });

  webDashServer.begin();
}

#endif
