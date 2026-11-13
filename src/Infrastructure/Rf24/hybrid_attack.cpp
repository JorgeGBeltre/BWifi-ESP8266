#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <RF24.h>
#include <U8g2lib.h>
#include "encoder_input.h"
#include "esp8266_compat.h"

extern "C" {
#include <user_interface.h>
}

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern RF24 jam1;
extern RF24 jam2;
extern bool runningApp;
extern bool isHybridActive;

static unsigned long lastUiUpdate = 0;
static int currentFakeChannel = 1;

void hybridAttackSetup() {
    WiFi.mode(WIFI_STA);
    wifi_set_opmode(STATION_MODE);
    
    jam1.begin();
    jam1.setPALevel(RF24_PA_MAX);
    jam1.setDataRate(RF24_2MBPS);
    jam1.startConstCarrier(RF24_PA_MAX, 45);
    
    if (g_dualRadioAvailable) {
        jam2.begin();
        jam2.setPALevel(RF24_PA_MAX);
        jam2.setDataRate(RF24_2MBPS);
        jam2.startConstCarrier(RF24_PA_MAX, 45);
    }
}

void hybridAttackLoop() {
    if (encOk()) {
        isHybridActive = !isHybridActive;
        if (isHybridActive) {
            hybridAttackSetup();
        } else {
            jam1.stopConstCarrier();
            if (g_dualRadioAvailable) jam2.stopConstCarrier();
            WiFi.mode(WIFI_OFF);
        }
    }

    if (encBack()) {
        isHybridActive = false;
        jam1.stopConstCarrier();
        if (g_dualRadioAvailable) jam2.stopConstCarrier();
        WiFi.mode(WIFI_OFF);
        runningApp = false;
        return;
    }

    if (isHybridActive) {
        jam1.setChannel(random(1, 80));
        if (g_dualRadioAvailable) jam2.setChannel(random(1, 80));
        wifi_set_channel(currentFakeChannel);
        currentFakeChannel = (currentFakeChannel % 13) + 1;
    }

    if (millis() - lastUiUpdate > 200) {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(15, 12, "MODO HIBRIDO TOTAL");
        u8g2.drawHLine(0, 15, 128);

        if (isHybridActive) {
            u8g2.drawBox(10, 22, 108, 22);
            u8g2.setDrawColor(0);
            u8g2.drawStr(18, 37, "CAOS EN EJECUCION");
            u8g2.setDrawColor(1);
            u8g2.setFont(u8g2_font_5x7_tr);
            u8g2.drawStr(20, 55, "NRF24: JAMMING...");
            u8g2.drawStr(20, 63, "WIFI:  INJECTING...");
        } else {
            u8g2.drawFrame(10, 22, 108, 22);
            u8g2.drawStr(30, 37, "SISTEMA LISTO");
            u8g2.setFont(u8g2_font_5x7_tr);
            u8g2.drawStr(10, 58, "[OK] INICIAR [HOLD] SALIR");
        }
        u8g2.sendBuffer();
        lastUiUpdate = millis();
    }
}

