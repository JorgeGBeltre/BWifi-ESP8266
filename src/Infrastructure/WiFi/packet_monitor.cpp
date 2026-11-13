#include "packet_monitor.h"
#include "encoder_input.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <U8g2lib.h>

extern "C" {
#include <user_interface.h>
}

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern bool runningApp;

static volatile uint32_t pkts_total = 0;
static volatile uint32_t pkts_data = 0;
static int current_ch = 1;
static uint32_t history_total[128];
static uint32_t history_data[128];
static unsigned long last_hop = 0;
static unsigned long last_draw = 0;
static bool autoHop = true;

static void IRAM_ATTR sniffer_callback(uint8_t *buf, uint16_t len) {
  pkts_total++;
  if (len > 12) {
    pkts_data++;
  }
}

void monitorSetup() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(20);
  wifi_set_opmode(STATION_MODE);
  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(sniffer_callback);
  wifi_set_channel(current_ch);
  wifi_promiscuous_enable(1);

  memset(history_total, 0, sizeof(history_total));
  memset(history_data, 0, sizeof(history_data));
}

void monitorLoop() {
  int d = encStep();
  if (d != 0) {
    autoHop = false;
    current_ch = ((current_ch - 1 + d) % 13);
    if (current_ch < 0) current_ch += 13;
    current_ch += 1;
    wifi_set_channel(current_ch);
  }

  if (encOk()) {
    autoHop = !autoHop;
    last_hop = millis();
  }

  if (autoHop && (millis() - last_hop > 1500)) {
    current_ch = (current_ch % 13) + 1;
    wifi_set_channel(current_ch);
    last_hop = millis();
  }

  if (millis() - last_draw > 100) {
    for (int i = 0; i < 127; i++) {
      history_total[i] = history_total[i + 1];
      history_data[i] = history_data[i + 1];
    }
    history_total[127] = pkts_total;
    history_data[127] = pkts_data;

    u8g2.clearBuffer();

    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(0, 9);
    u8g2.print("CH:");
    u8g2.print(current_ch);
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(32, 8, autoHop ? "[AUTO SCAN]" : "[MANUAL GIRO]");

    u8g2.setCursor(96, 8);
    u8g2.print("P:");
    u8g2.print((unsigned long)pkts_total);
    u8g2.drawHLine(0, 11, 128);

    for (int x = 0; x < 128; x++) {
      int h_tot = map(history_total[x], 0, 50, 0, 48);
      int h_dat = map(history_data[x], 0, 50, 0, 48);
      if (h_tot > 48) h_tot = 48;
      if (h_dat > 48) h_dat = 48;

      if (h_tot > 0)
        u8g2.drawVLine(x, 62 - h_tot, h_tot);

      if (h_dat > 0) {
        for (int y = 62; y > 62 - h_dat; y -= 2) {
          u8g2.drawPixel(x, y);
        }
      }
    }

    if (pkts_total > 100) {
      u8g2.setDrawColor(1);
      u8g2.drawBox(25, 25, 78, 15);
      u8g2.setDrawColor(0);
      u8g2.setFont(u8g2_font_6x10_tr);
      u8g2.drawStr(30, 36, "HIGH TRAFFIC!");
      u8g2.setDrawColor(1);
    }

    u8g2.sendBuffer();

    pkts_total = 0;
    pkts_data = 0;
    last_draw = millis();
  }
}

