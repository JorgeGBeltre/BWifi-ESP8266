#include <Arduino.h>
#include <U8g2lib.h>
#include <RF24.h>
#include "encoder_input.h"
#include "esp8266_compat.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern RF24 jam1;
extern RF24 jam2;
extern bool runningApp;

bool isBtJamming = false;

byte bt_channels[] = {
  2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40,
  42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80
};

int ptr_bt1 = 0;
int ptr_bt2 = 20; 

void drawBtUI() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(20, 12, "BLUETOOTH JAMMER");
    u8g2.drawHLine(0, 15, 128);

    if (isBtJamming) {
        u8g2.drawBox(10, 22, 108, 20);
        u8g2.setDrawColor(0);
        u8g2.drawStr(18, 36, "ATAQUE BT ACTIVO");
        u8g2.setDrawColor(1);
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(22, 52, "Saltando 40 Canales");
        u8g2.drawStr(18, 61, "[OK/HOLD] DETENER");
    } else {
        u8g2.drawFrame(10, 22, 108, 20);
        u8g2.drawStr(28, 36, "SISTEMA LISTO");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(15, 52, "Frec: 2402 - 2480 MHz");
        u8g2.drawStr(10, 61, "[OK] INICIAR [HOLD] SALIR");
    }
    u8g2.sendBuffer();
}

void btJammerSetup() {
    isBtJamming = false;
    jam1.begin();
    jam1.setAutoAck(false);
    jam1.setPALevel(RF24_PA_MAX);
    jam1.setDataRate(RF24_1MBPS); 
    jam1.setCRCLength(RF24_CRC_DISABLED);
    jam1.stopListening();

    if (g_dualRadioAvailable) {
        jam2.begin();
        jam2.setAutoAck(false);
        jam2.setPALevel(RF24_PA_MAX);
        jam2.setDataRate(RF24_1MBPS);
        jam2.setCRCLength(RF24_CRC_DISABLED);
        jam2.stopListening();
    }

    drawBtUI();
}

void btJammerLoop() {
    if (encOk()) {
        isBtJamming = !isBtJamming;
        if (isBtJamming) {
            jam1.startConstCarrier(RF24_PA_MAX, bt_channels[0]);
            if (g_dualRadioAvailable) jam2.startConstCarrier(RF24_PA_MAX, bt_channels[20]);
        } else {
            jam1.stopConstCarrier();
            if (g_dualRadioAvailable) jam2.stopConstCarrier();
        }
        drawBtUI();
    }

    if (encBack()) {
        isBtJamming = false;
        jam1.stopConstCarrier();
        if (g_dualRadioAvailable) jam2.stopConstCarrier();
        runningApp = false;
        return;
    }

    if (isBtJamming) {
        for(int i = 0; i < 40; i++) {
            ptr_bt1 = (ptr_bt1 + 1) % 40;
            ptr_bt2 = (ptr_bt2 + 1) % 40;
            jam1.setChannel(bt_channels[ptr_bt1]);
            if (g_dualRadioAvailable) jam2.setChannel(bt_channels[ptr_bt2]);
        }
    }
}

