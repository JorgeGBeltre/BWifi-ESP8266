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
static int s_remoteSubMode = 0; // 0: Media, 1: Mouse, 2: Reels, 3: Ducky Script, 4: Layout ES/EN, 5: Gamepad
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
  char pkt[48];
  int len = snprintf(pkt, sizeof(pkt), "BWIFI_HID:%s:%d:%s", cmd, val, s_layoutEs ? "ES" : "EN");
  IPAddress bcast(255, 255, 255, 255);
  s_udp.beginPacket(bcast, 4210);
  s_udp.write((const uint8_t *)pkt, len);
  s_udp.endPacket();

  uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_send(broadcastMac, (uint8_t *)pkt, len);
  s_txCount++;
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

static void executeDuckyScriptFromFs() {
  ensureDefaultDuckyFile();
  File f = SPIFFS.open("/payload.txt", "r");
  if (!f) {
    sendRemoteEvent("EXEC_PAYLOAD", 1);
    s_duckyLinesExecuted = 1;
    return;
  }

  s_duckyLinesExecuted = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0 || line.startsWith("REM")) continue;

    if (line.startsWith("DELAY ")) {
      int ms = line.substring(6).toInt();
      delay(constrain(ms, 10, 2000));
    } else if (line.startsWith("STRING ")) {
      sendRemoteEvent("DUCKY_STR", line.substring(7).length());
    } else if (line == "ENTER") {
      sendRemoteEvent("KEY_ENTER", 1);
    } else if (line == "TAB") {
      sendRemoteEvent("KEY_TAB", 1);
    } else if (line == "SPACE") {
      sendRemoteEvent("KEY_SPACE", 1);
    } else if (line.startsWith("GUI")) {
      sendRemoteEvent("KEY_GUI", 1);
    }
    s_duckyLinesExecuted++;
    delay(20);
  }
  f.close();
}

void btRemoteLoop() {
  ensureRemoteNet();
  ensureDefaultDuckyFile();

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);

  if (!s_modeSelected) {
    const int kTotalModes = 6;
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
    snprintf(layoutLine, sizeof(layoutLine), "5. LAYOUT TECLADO: %s", s_layoutEs ? "ES" : "EN");

    const char *modes[kTotalModes] = {
        "1. CONTROL MEDIA",
        "2. MOUSE (GIRO)",
        "3. NAV / REELS / PPT",
        "4. DUCKY (/payload.txt)",
        layoutLine,
        "6. GAMEPAD / STICK"};

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
      } else {
        s_modeSelected = true;
        s_mouseAction = 0;
      }
      encClear();
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
      u8g2.printf("File: /payload.txt [%s]", s_layoutEs ? "ES" : "EN");
      u8g2.drawStr(4, 35, "[OK]  Ejecutar Ducky Script");
      u8g2.setCursor(4, 46);
      u8g2.printf("Ultima ejec: %d lineas", s_duckyLinesExecuted);
      u8g2.drawStr(4, 59, "Edita en Web | HOLD:Salir");

      if (encOk()) {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(8, 28, "EJECUTANDO DUCKY...");
        u8g2.sendBuffer();
        executeDuckyScriptFromFs();
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
