#include "about_info.h"
#include "encoder_input.h"
#include <ESP8266WiFi.h>
#include <U8g2lib.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern bool runningApp;

uint32_t g_bootCount = 0;

static float scrollTextY = 64;
static int dropY[16];
static bool matrixInit = false;

void aboutLoop() {
  if (!matrixInit) {
    for (int i = 0; i < 16; i++)
      dropY[i] = random(-64, 0);
    matrixInit = true;
  }

  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_4x6_tr);
  for (int i = 0; i < 16; i++) {
    char randomChar = (char)random(33, 126);
    u8g2.drawGlyph(i * 8, dropY[i], randomChar);
    dropY[i] += 2;
    if (dropY[i] > 64)
      dropY[i] = random(-20, 0);
  }

  int step = encStep();
  if (step != 0) {
    scrollTextY -= step * 5.0f;
  } else {
    scrollTextY -= 0.45f;
  }
  if (scrollTextY < -145)
    scrollTextY = 64;
  if (scrollTextY > 64)
    scrollTextY = 64;

  int y = (int)scrollTextY;

  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(16, y, "BWifi8266 v3.5");

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(10, y + 15, "Creado por: GeorgeDev");
  u8g2.drawStr(10, y + 27, "MCU: ESP8266EX @ 160MHz");
  u8g2.drawStr(10, y + 39, "Radios: nRF24L01+ PA+LNA");
  u8g2.drawStr(10, y + 51, "Control: Encoder HW-040");

  char buf[32];
  snprintf(buf, sizeof(buf), "Boots: %lu | RST: %s", (unsigned long)g_bootCount, ESP.getResetReason().c_str());
  buf[25] = '\0';
  u8g2.drawStr(10, y + 63, buf);

  snprintf(buf, sizeof(buf), "RAM Libre: %u KB", (unsigned)(ESP.getFreeHeap() / 1024));
  u8g2.drawStr(10, y + 75, buf);

  u8g2.drawStr(10, y + 87, "Build: " __DATE__);
  u8g2.drawStr(18, y + 102, "--- GRACIAS POR USAR ---");

  u8g2.setDrawColor(0);
  u8g2.drawBox(0, 0, 128, 12);
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(22, 9, "SISTEMA INFO v3.5");
  u8g2.drawHLine(0, 11, 128);

  u8g2.sendBuffer();

  if (encBack()) {
    scrollTextY = 64;
    runningApp = false;
  }
}

