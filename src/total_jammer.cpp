#include <Arduino.h>
#include <U8g2lib.h>
#include <RF24.h>
#include "encoder_input.h"
#include "esp8266_compat.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern RF24 jam1;
extern RF24 jam2;
extern bool runningApp;
extern bool isTotalAttacking;

const uint8_t full_hop_list[] = {
    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
    22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
    43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64,
    65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85
};

const int num_channels = sizeof(full_hop_list) / sizeof(full_hop_list[0]);
int ptr1 = 0;
int ptr2 = num_channels / 2;

void drawTotalDashboard() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(12, 12, "MODO BARRIDO TOTAL");
    u8g2.drawHLine(0, 15, 128);

    if (isTotalAttacking) {
        u8g2.drawBox(10, 22, 108, 20);
        u8g2.setDrawColor(0);
        u8g2.drawStr(20, 36, "DESTRUYENDO...");
        u8g2.setDrawColor(1);
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(10, 55, "[OK/HOLD] DETENER");
    } else {
        u8g2.drawFrame(10, 22, 108, 20);
        u8g2.drawStr(32, 36, "SISTEMA LISTO");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(10, 52, "Cubre: WiFi + BT");
        u8g2.drawStr(10, 61, "[OK] INICIAR [HOLD] SALIR");
    }
    u8g2.sendBuffer();
}

void totalJammerSetup() {
    isTotalAttacking = false;
    jam1.begin();
    jam1.stopListening();
    jam1.setPALevel(RF24_PA_MAX);
    jam1.setDataRate(RF24_2MBPS);
    jam1.setAutoAck(false);
    
    if (g_dualRadioAvailable) {
        jam2.begin();
        jam2.stopListening();
        jam2.setPALevel(RF24_PA_MAX);
        jam2.setDataRate(RF24_2MBPS);
        jam2.setAutoAck(false);
    }

    drawTotalDashboard();
}

void totalJammerLoop() {
    if (encOk()) {
        isTotalAttacking = !isTotalAttacking;
        if (isTotalAttacking) {
            jam1.startConstCarrier(RF24_PA_MAX, 45);
            if (g_dualRadioAvailable) jam2.startConstCarrier(RF24_PA_MAX, 45);
        } else {
            jam1.stopConstCarrier();
            if (g_dualRadioAvailable) jam2.stopConstCarrier();
        }
        drawTotalDashboard();
    }

    if (encBack()) {
        isTotalAttacking = false;
        jam1.stopConstCarrier();
        if (g_dualRadioAvailable) jam2.stopConstCarrier();
        runningApp = false;
        return;
    }

    if (isTotalAttacking) {
        ptr1++;
        if (ptr1 >= num_channels) ptr1 = 0;

        ptr2--;
        if (ptr2 < 0) ptr2 = num_channels - 1;

        jam1.setChannel(full_hop_list[ptr1]);
        if (g_dualRadioAvailable) jam2.setChannel(full_hop_list[ptr2]);
    }
}

