#include "about_info.h"
#include "encoder_input.h"
#include "esp8266_compat.h"
#include <ESP8266WiFi.h>
#include <U8g2lib.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern bool runningApp;

uint32_t g_bootCount = 0;

static float scrollTextY = 64.0f;
static int dropY[16];
static bool matrixInit = false;

void aboutLoop() {
  if (!matrixInit) {
    for (int i = 0; i < 16; i++)
      dropY[i] = random(-64, 0);
    matrixInit = true;
  }

  u8g2.clearBuffer();

  // --- FONDO: LLUVIA MATRIX ---
  u8g2.setFont(u8g2_font_4x6_tr);
  for (int i = 0; i < 16; i++) {
    char randomChar = (char)random(33, 126);
    u8g2.drawGlyph(i * 8, dropY[i], randomChar);
    dropY[i] += 2;
    if (dropY[i] > 64)
      dropY[i] = random(-20, 0);
  }

  // --- VENTANA DE TERMINAL ENMARCADA ---
  int winX = 8, winY = 6, winW = 112, winH = 52;
  u8g2.setDrawColor(0);
  u8g2.drawBox(winX, winY, winW, winH);
  u8g2.setDrawColor(1);
  u8g2.drawFrame(winX, winY, winW, winH);
  u8g2.drawHLine(winX, winY + 9, winW);

  u8g2.setFont(u8g2_font_4x6_tr);
  u8g2.drawStr(winX + 4, winY + 7, "> TERMINAL_INFO v3.5");
  if ((millis() / 300) % 2 == 0) {
    u8g2.drawBox(winX + 88, winY + 2, 4, 5);
  }

  // --- SCROLL DEL CONTENIDO DE LA TERMINAL ---
  int step = encStep();
  if (step != 0) {
    scrollTextY -= step * 5.0f;
  } else {
    scrollTextY -= 0.45f;
  }
  if (scrollTextY < -110.0f)
    scrollTextY = 58.0f;
  if (scrollTextY > 58.0f)
    scrollTextY = 58.0f;

  int y = (int)scrollTextY;

  u8g2.setClipWindow(winX + 2, winY + 11, winX + winW - 2, winY + winH - 2);
  u8g2.setFont(u8g2_font_5x7_tr);

  char lineBuf[28];
  u8g2.drawStr(winX + 5, y, "System: BWifi8266 v3.5");
  snprintf(lineBuf, sizeof(lineBuf), "HW: ESP8266+%s", g_dualRadioAvailable ? "Dual RF24" : "1x RF24");
  u8g2.drawStr(winX + 5, y + 10, lineBuf);
  u8g2.drawStr(winX + 5, y + 20, "Input: Encoder HW-040");
  snprintf(lineBuf, sizeof(lineBuf), "RAM Libre: %u KB", (unsigned)(ESP.getFreeHeap() / 1024));
  u8g2.drawStr(winX + 5, y + 30, lineBuf);
  snprintf(lineBuf, sizeof(lineBuf), "Boots:%lu RST:%s", (unsigned long)g_bootCount, ESP.getResetReason().c_str());
  lineBuf[21] = '\0';
  u8g2.drawStr(winX + 5, y + 40, lineBuf);
  u8g2.drawStr(winX + 5, y + 50, "Build: " __DATE__);
  u8g2.drawStr(winX + 5, y + 60, "Mode: Security Suite");
  u8g2.drawStr(winX + 5, y + 75, "Author: Jorge Beltre");
  u8g2.setMaxClipWindow();

  u8g2.sendBuffer();

  if (encOk() || encBack()) {
    scrollTextY = 64.0f;
    runningApp = false;
    encClear();
  }
}
