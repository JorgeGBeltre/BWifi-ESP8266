#include "web_dashboard.h"
#include "encoder_input.h"
#include "web_server.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <U8g2lib.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern bool runningApp;
extern String apSSID;

void webDashboardLoop() {
  webDashServer.handleClient();

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawRBox(0, 0, 128, 14, 2);
  u8g2.setDrawColor(0);
  u8g2.drawStr(18, 11, "MODO SERVIDOR WEB");
  u8g2.setDrawColor(1);

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setCursor(5, 25);
  u8g2.print("1. WiFi: ");
  u8g2.print(apSSID);
  u8g2.drawStr(5, 34, "2. Pass: 12345678");
  u8g2.drawStr(5, 43, "3. Entra en navegador:");

  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawFrame(15, 46, 98, 12);
  u8g2.drawStr(24, 55, "192.168.4.1");

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setCursor(5, 63);
  u8g2.print("Clientes: ");
  u8g2.print(WiFi.softAPgetStationNum());
  u8g2.drawStr(62, 63, "[HOLD: SALIR]");

  u8g2.sendBuffer();
}

