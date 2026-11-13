#include "bt_remote.h"
#include "encoder_input.h"
#include "esp8266_compat.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <U8g2lib.h>

extern "C" {
#include <espnow.h>
}

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern bool runningApp;

static WiFiUDP s_udp;
static bool s_netReady = false;
static int s_remoteSubMode = 0; // 0: Media, 1: Mouse, 2: Reels, 3: Ducky Script, 4: Layout ES/EN, 5: Gamepad, 6: Macro Rapida
static bool s_modeSelected = false;
static int s_mouseAction = 0;
static uint32_t s_txCount = 0;
static bool s_layoutEs = true;
static int s_duckyLinesExecuted = 0;

static void ensureRemoteNet() {
  if (!s_netReady) {
    if (WiFi.getMode() == WIFI_OFF) {
      WiFi.mode(WIFI_AP_STA);
    }
    s_udp.begin(4210);
    s_netReady = true;
  }
}

static void sendRemoteEvent(const char *cmd, int val) {
  ensureRemoteNet();
  char pkt[64];
  int len = snprintf(pkt, sizeof(pkt), "BWIFI_HID:%s:%d:%s", cmd, val, s_layoutEs ? "ES" : "US");
  IPAddress bcast(255, 255, 255, 255);
  s_udp.beginPacket(bcast, 4210);
  s_udp.write((const uint8_t *)pkt, len);
  s_udp.endPacket();

  uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_send(broadcastMac, (uint8_t *)pkt, len);
  s_txCount++;
}

// Mapeo de caracteres US -> ES ISO para transmisor HID / Ducky Script
static String mapWithLayout(const String &str) {
  if (!s_layoutEs) {
    return str;
  }
  String out;
  out.reserve(str.length());
  for (size_t i = 0; i < str.length(); i++) {
    char c = str[i];
    if (c == '/') out += '&';
    else if (c == ':') out += '>';
    else if (c == '-') out += '/';
    else if (c == '_') out += '?';
    else if (c == ';') out += '<';
    else out += c;
  }
  return out;
}

static void sendRemoteString(const String &text) {
  ensureRemoteNet();
  String mapped = mapWithLayout(text);
  // Enviar en bloques de hasta 160 bytes por trama UDP / 200 bytes ESP-NOW
  int total = mapped.length();
  int offset = 0;
  uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  IPAddress bcast(255, 255, 255, 255);

  while (offset < total) {
    String chunk = mapped.substring(offset, min(offset + 140, total));
    char pkt[192];
    int len = snprintf(pkt, sizeof(pkt), "BWIFI_HID:DUCKY_STR:%d:%s:%s",
                       (int)chunk.length(), s_layoutEs ? "ES" : "US", chunk.c_str());
    if (len < 0) break;
    if (len >= (int)sizeof(pkt)) len = sizeof(pkt) - 1;

    s_udp.beginPacket(bcast, 4210);
    s_udp.write((const uint8_t *)pkt, len);
    s_udp.endPacket();

    esp_now_send(broadcastMac, (uint8_t *)pkt, len);
    s_txCount++;
    offset += 140;
    delay(8);
  }
}

static void ensureDefaultDuckyFile() {
  if (!SPIFFS.begin()) return;
  if (!SPIFFS.exists("/payload.txt")) {
    File f = SPIFFS.open("/payload.txt", "w");
    if (f) {
      f.println("REM BWifi Ducky Script");
      f.println("DELAY 400");
      f.println("GUI r");
      f.println("DELAY 300");
      f.println("STRING notepad");
      f.println("ENTER");
      f.println("DELAY 500");
      f.println("STRING Hola desde BWifi-ESP8266 v3.5 Ducky Parser!");
      f.println("ENTER");
      f.close();
    }
  }
}

static void runQuickMacro() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(10, 26, "EJECUTANDO MACRO...");
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(10, 42, "GUI+R -> Notepad Demo");
  u8g2.sendBuffer();

  sendRemoteEvent("KEY_GUI", (int)'r');
  delay(350);
  sendRemoteString("notepad");
  delay(80);
  sendRemoteEvent("KEY_ENTER", 1);
  delay(450);
  sendRemoteString("BWifi-ESP8266 v3.5 - Remote HID Macro OK!");
  sendRemoteEvent("KEY_ENTER", 1);
  encClear();
}

static void executeDuckyScriptFromFs() {
  ensureDefaultDuckyFile();
  File f = SPIFFS.open("/payload.txt", "r");
  if (!f || f.size() == 0) {
    if (f) f.close();
    runQuickMacro();
    s_duckyLinesExecuted = 1;
    return;
  }

  size_t totalBytes = f.size();
  size_t readBytes = 0;
  s_duckyLinesExecuted = 0;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    readBytes += line.length() + 1;
    line.trim();
    if (line.length() == 0 || line.startsWith("REM")) continue;

    s_duckyLinesExecuted++;

    // Barra de progreso en tiempo real en OLED
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(6, 12, "DUCKY SCRIPT /FS");
    u8g2.drawHLine(0, 14, 128);
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(6, 26);
    u8g2.printf("Linea #%d [%s]", s_duckyLinesExecuted, s_layoutEs ? "ES" : "US");
    u8g2.drawFrame(14, 34, 100, 8);
    int barW = (totalBytes > 0) ? (int)((readBytes * 96) / totalBytes) : 48;
    if (barW > 96) barW = 96;
    u8g2.drawBox(16, 36, barW, 4);
    u8g2.setCursor(6, 54);
    u8g2.print(line.substring(0, 22));
    u8g2.sendBuffer();

    if (line.startsWith("DELAY ")) {
      int ms = line.substring(6).toInt();
      delay(constrain(ms, 10, 5000));
    } else if (line.startsWith("STRING ")) {
      sendRemoteString(line.substring(7));
    } else if (line == "ENTER") {
      sendRemoteEvent("KEY_ENTER", 1);
    } else if (line == "TAB") {
      sendRemoteEvent("KEY_TAB", 1);
    } else if (line == "SPACE") {
      sendRemoteEvent("KEY_SPACE", 1);
    } else if (line == "UP" || line == "UPARROW") {
      sendRemoteEvent("KEY_UP", 1);
    } else if (line == "DOWN" || line == "DOWNARROW") {
      sendRemoteEvent("KEY_DOWN", 1);
    } else if (line == "LEFT" || line == "LEFTARROW") {
      sendRemoteEvent("KEY_LEFT", 1);
    } else if (line == "RIGHT" || line == "RIGHTARROW") {
      sendRemoteEvent("KEY_RIGHT", 1);
    } else if (line.startsWith("GUI")) {
      int keyVal = 1;
      if (line.length() >= 5) {
        char c = line.charAt(4);
        keyVal = (int)tolower(c);
      }
      sendRemoteEvent("KEY_GUI", keyVal);
    }
    delay(25);

    if (encBack()) {
      f.close();
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tr);
      u8g2.drawStr(16, 36, "SCRIPT ABORTADO");
      u8g2.sendBuffer();
      delay(400);
      encClear();
      return;
    }
  }
  f.close();

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(14, 34, "PAYLOAD COMPLETADO");
  u8g2.sendBuffer();
  delay(500);
  encClear();
}

void btRemoteLoop() {
  ensureRemoteNet();
  ensureDefaultDuckyFile();

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);

  if (!s_modeSelected) {
    const int kTotalModes = 7;
    int d = encStep();
    if (d != 0) {
      s_remoteSubMode = (s_remoteSubMode + d) % kTotalModes;
      if (s_remoteSubMode < 0) s_remoteSubMode += kTotalModes;
    }

    u8g2.drawRBox(0, 0, 128, 12, 2);
    u8g2.setDrawColor(0);
    u8g2.drawStr(6, 9, "REMOTE HID + DUCKY");
    u8g2.setDrawColor(1);

    char layoutLine[26];
    snprintf(layoutLine, sizeof(layoutLine), "5. LAYOUT TECLADO: %s", s_layoutEs ? "ES" : "US");

    const char *modes[kTotalModes] = {
        "1. CONTROL MEDIA",
        "2. MOUSE (GIRO)",
        "3. NAV / REELS / PPT",
        "4. DUCKY (/payload.txt)",
        layoutLine,
        "6. GAMEPAD / STICK",
        "7. MACRO RAPIDA"};

    int firstVis = s_remoteSubMode - 2;
    if (firstVis > kTotalModes - 4) firstVis = kTotalModes - 4;
    if (firstVis < 0) firstVis = 0;

    u8g2.setFont(u8g2_font_5x7_tr);
    for (int row = 0; row < 4; row++) {
      int i = firstVis + row;
      int y = 24 + row * 10;
      if (i == s_remoteSubMode) {
        u8g2.drawRBox(4, y - 7, 120, 9, 2);
        u8g2.setDrawColor(0);
        u8g2.drawStr(8, y, modes[i]);
        u8g2.setDrawColor(1);
      } else {
        u8g2.drawStr(8, y, modes[i]);
      }
    }

    u8g2.drawHLine(0, 54, 128);
    u8g2.drawStr(4, 62, "UDP:4210 | [OK] Entrar");

    if (encOk()) {
      if (s_remoteSubMode == 4) {
        s_layoutEs = !s_layoutEs;
      } else if (s_remoteSubMode == 6) {
        runQuickMacro();
      } else {
        s_modeSelected = true;
        s_mouseAction = 0;
      }
      encClear();
    }
    if (encAux()) {
      executeDuckyScriptFromFs();
    }
    if (encBack()) {
      s_udp.stop();
      s_netReady = false;
      runningApp = false;
      encClear();
      return;
    }
  } else {
    if (s_remoteSubMode == 0) {
      u8g2.drawStr(14, 11, "MODO: MULTIMEDIA");
      u8g2.drawHLine(0, 13, 128);
      u8g2.setFont(u8g2_font_5x7_tr);
      u8g2.drawStr(6, 26, "GIRA: Subir/Bajar Volumen");
      u8g2.drawStr(6, 37, "OK:   Play / Pause");
      u8g2.setCursor(6, 48);
      u8g2.printf("Paquetes TX: %lu", (unsigned long)s_txCount);
      u8g2.drawStr(6, 60, "HOLD: Volver Selector");

      int step = encStep();
      if (step < 0) sendRemoteEvent("VOL_UP", -step);
      else if (step > 0) sendRemoteEvent("VOL_DOWN", step);
      if (encOk()) sendRemoteEvent("PLAY_PAUSE", 1);

    } else if (s_remoteSubMode == 1) {
      const char *actNames[5] = {
          "MOVER EJE X (H)",
          "MOVER EJE Y (V)",
          "CLIC IZQUIERDO",
          "CLIC DERECHO",
          "RUEDA SCROLL"};

      u8g2.drawStr(10, 11, "MODO: MOUSE REMOTO");
      u8g2.drawHLine(0, 13, 128);
      u8g2.setFont(u8g2_font_5x7_tr);
      u8g2.setCursor(4, 25);
      u8g2.printf("Accion: %s", actNames[s_mouseAction]);
      u8g2.drawStr(4, 36, "GIRA: Mover / Clic / Scroll");
      u8g2.drawStr(4, 47, "OK:   Cambiar Eje / Clic");
      u8g2.drawStr(4, 59, "HOLD: Volver Selector");

      if (encOk()) {
        s_mouseAction = (s_mouseAction + 1) % 5;
      }
      int step = encStep();
      if (step != 0) {
        if (s_mouseAction == 0) sendRemoteEvent("MOUSE_X", step * 14);
        else if (s_mouseAction == 1) sendRemoteEvent("MOUSE_Y", step * 14);
        else if (s_mouseAction == 2) sendRemoteEvent("CLICK_L", 1);
        else if (s_mouseAction == 3) sendRemoteEvent("CLICK_R", 1);
        else if (s_mouseAction == 4) sendRemoteEvent("SCROLL", -step);
      }

    } else if (s_remoteSubMode == 2) {
      u8g2.drawStr(10, 11, "MODO: REELS / PPT");
      u8g2.drawHLine(0, 13, 128);
      u8g2.setFont(u8g2_font_5x7_tr);
      u8g2.drawStr(6, 26, "GIRA: Siguiente / Anterior");
      u8g2.drawStr(6, 38, "OK:   Pausa / Espacio");
      u8g2.drawStr(6, 58, "HOLD: Volver Selector");

      int step = encStep();
      if (step > 0) sendRemoteEvent("KEY_DOWN", step);
      else if (step < 0) sendRemoteEvent("KEY_UP", -step);
      if (encOk()) sendRemoteEvent("KEY_SPACE", 1);

    } else if (s_remoteSubMode == 3) {
      u8g2.drawStr(6, 11, "DUCKY SCRIPT PARSER");
      u8g2.drawHLine(0, 13, 128);
      u8g2.setFont(u8g2_font_5x7_tr);
      u8g2.setCursor(4, 24);
      u8g2.printf("File: /payload.txt [%s]", s_layoutEs ? "ES" : "US");
      u8g2.drawStr(4, 34, "[OK]  Ejecutar /payload.txt");
      u8g2.drawStr(4, 44, "[AUX] Macro Rapida");
      u8g2.setCursor(4, 53);
      u8g2.printf("Ultima ejec: %d lineas", s_duckyLinesExecuted);
      u8g2.drawStr(4, 62, "Edita en Web | HOLD:Salir");

      if (encOk()) {
        executeDuckyScriptFromFs();
      } else if (encAux()) {
        runQuickMacro();
      }
    } else if (s_remoteSubMode == 5) {
      u8g2.drawStr(8, 11, "MODO: GAMEPAD STICK");
      u8g2.drawHLine(0, 13, 128);
      u8g2.setFont(u8g2_font_5x7_tr);
      u8g2.drawStr(4, 25, "GIRA: Stick Izq / Der");
      u8g2.drawStr(4, 36, "OK:   Boton Accion (A/FIRE)");
      u8g2.drawStr(4, 47, "SW+GIRA: Arriba / Abajo");
      u8g2.drawStr(4, 59, "HOLD: Volver Selector");

      int step = encStep();
      if (step != 0) {
        if (encSwPressed()) sendRemoteEvent("PAD_Y", step);
        else sendRemoteEvent("PAD_X", step);
      }
      if (encOk()) {
        sendRemoteEvent("PAD_BTN_A", 1);
      }
    }

    if (encBack()) {
      s_modeSelected = false;
      encClear();
    }
  }

  u8g2.sendBuffer();
}
