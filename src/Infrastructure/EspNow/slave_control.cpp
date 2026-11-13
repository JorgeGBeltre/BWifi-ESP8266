#include "slave_control.h"
#include "encoder_input.h"
#include "settings_menu.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <U8g2lib.h>

extern "C" {
#include <espnow.h>
}

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern bool runningApp;

uint8_t macEsclavo[] = {0x78, 0x1C, 0x3C, 0xE6, 0x09, 0x54};

typedef struct struct_message {
  bool activar;
} struct_message;

struct_message myData;
bool estadoEsclavo = false;

void slaveControlSetup() {
  estadoEsclavo = g_settings.slaveState;
  myData.activar = estadoEsclavo;

  WiFi.mode(WIFI_AP_STA);
  if (esp_now_init() != 0) {
    return;
  }
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_add_peer(macEsclavo, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);
}

void slaveControlLoop() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x12_tr);
  u8g2.drawStr(15, 12, "MANDO A DISTANCIA");
  u8g2.drawHLine(0, 15, 128);

  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(10, 32, "ESTADO GUARDADO:");

  if (estadoEsclavo) {
    u8g2.drawBox(10, 38, 108, 15);
    u8g2.setDrawColor(0);
    u8g2.drawStr(42, 49, "ACTIVO");
    u8g2.setDrawColor(1);
  } else {
    u8g2.drawFrame(10, 38, 108, 15);
    u8g2.drawStr(40, 49, "APAGADO");
  }

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(8, 62, "[OK] CAMBIAR [HOLD] SALIR");
  u8g2.sendBuffer();

  if (encOk()) {
    estadoEsclavo = !estadoEsclavo;
    myData.activar = estadoEsclavo;

    g_settings.slaveState = estadoEsclavo;
    settingsSave();

    WiFi.mode(WIFI_AP_STA);
    for (int i = 0; i < 5; i++) {
      esp_now_send(macEsclavo, (uint8_t *)&myData, sizeof(myData));
      delay(15);
    }
  }

  if (encBack()) {
    runningApp = false;
  }
}

