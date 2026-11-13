#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <U8g2lib.h>
#include "encoder_input.h"

extern "C" {
#include <user_interface.h>
}

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern bool runningApp;

static volatile int deauth_detected = 0;
bool attack_confirmed = false;
static int centinela_channel = 1;
static unsigned long last_check = 0;
static bool channel_fixed = false;

static void IRAM_ATTR sniffer_centinela(uint8_t *buf, uint16_t len) {
  if (len > 12) {
    uint8_t fc = buf[12]; // Primer byte de Frame Control tras RxControl (12 bytes)
    if (fc == 0xC0 || fc == 0xA0) {
      deauth_detected++;
    }
  } else {
    deauth_detected++;
  }
}

void centinelaSetup() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(20);
  wifi_set_opmode(STATION_MODE);
  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(sniffer_centinela);
  wifi_set_channel(centinela_channel);
  wifi_promiscuous_enable(1);
}

void centinelaLoop() {
  if (!channel_fixed) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(15, 12, "CONFIG. CENTINELA");
    u8g2.drawHLine(0, 14, 128);

    u8g2.drawStr(10, 32, "Canal de Escucha:");
    u8g2.setFont(u8g2_font_9x18B_tr);
    u8g2.setCursor(55, 52);
    u8g2.print(centinela_channel);

    int d = encStep();
    if (d != 0) {
      centinela_channel = ((centinela_channel - 1 + d) % 13);
      if (centinela_channel < 0) centinela_channel += 13;
      centinela_channel += 1;
    }

    if (encOk()) {
      centinelaSetup();
      channel_fixed = true;
      encClear();
    }

    if (encBack()) {
      wifi_promiscuous_enable(0);
      wifi_set_promiscuous_rx_cb(NULL);
      runningApp = false;
      encClear();
      return;
    }

    u8g2.sendBuffer();
  } else {
    if (millis() - last_check >= 1000) {
      attack_confirmed = (deauth_detected > 80);
      deauth_detected = 0;
      last_check = millis();
    }

    u8g2.clearBuffer();

    if (attack_confirmed) {
      if ((millis() / 200) % 2 == 0) {
        u8g2.drawBox(0, 0, 128, 64);
        u8g2.setDrawColor(0);
      }
      u8g2.setFont(u8g2_font_6x12_tr);
      u8g2.drawStr(10, 22, "!ATAQUE DETECTADO!");
      u8g2.setFont(u8g2_font_6x10_tr);
      u8g2.drawStr(18, 40, "DEAUTH / JAMMING");
      u8g2.drawStr(25, 55, "EN CANAL: ");
      u8g2.setCursor(85, 55);
      u8g2.print(centinela_channel);
      u8g2.setDrawColor(1);
    } else {
      u8g2.setFont(u8g2_font_6x10_tr);
      u8g2.drawStr(5, 10, "CENTINELA: VIGILANDO");
      u8g2.drawHLine(0, 12, 128);

      u8g2.setCursor(10, 28);
      u8g2.print("Canal Fijo: ");
      u8g2.print(centinela_channel);

      int barWidth = map(constrain(deauth_detected, 0, 80), 0, 80, 0, 100);
      u8g2.drawFrame(10, 35, 104, 10);
      u8g2.drawBox(12, 37, barWidth, 6);

      u8g2.setFont(u8g2_font_5x7_tr);
      u8g2.setCursor(10, 56);
      u8g2.print("Pkts Sospechosos: ");
      u8g2.print(deauth_detected);
      u8g2.drawStr(10, 63, "[OK/HOLD: CAMBIAR CH]");
    }

    if (encOk() || encBack()) {
      wifi_promiscuous_enable(0);
      wifi_set_promiscuous_rx_cb(NULL);
      channel_fixed = false;
      attack_confirmed = false;
      encClear();
    }

    u8g2.sendBuffer();
  }
}

