#include <Arduino.h>
#include <U8g2lib.h>
#include <RF24.h>
#include "encoder_input.h"
#include "esp8266_compat.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern RF24 jam1;
extern RF24 jam2;
extern bool runningApp;

int wifi_channel = 1;
bool jamming = false;

uint8_t noise[32] = {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55,
                     0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00,
                     0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55,
                     0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00};

void configureRadio(RF24 &radio) {
    radio.begin();
    radio.setAutoAck(false);
    radio.setPALevel(RF24_PA_MAX);
    radio.setDataRate(RF24_2MBPS);
    radio.setCRCLength(RF24_CRC_DISABLED);
    radio.stopListening();
}

void jammerSetup() {
    configureRadio(jam1);
    if (g_dualRadioAvailable) configureRadio(jam2);
    jamming = false;
}

void jammerLoop() {
    u8g2.clearBuffer();

    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(20, 10, "JAMMER DE CANAL");
    u8g2.drawHLine(0, 12, 128);

    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.setCursor(10, 28);
    u8g2.print("CANAL WIFI: ");
    u8g2.print(wifi_channel);

    int base_rf = (wifi_channel - 1) * 5 + 1;
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(10, 38);
    u8g2.print("Frec: "); u8g2.print(2400 + base_rf + 11); u8g2.print(" MHz");

    int d = encStep();
    if (d != 0) {
        wifi_channel = ((wifi_channel - 1 + d) % 14);
        if (wifi_channel < 0) wifi_channel += 14;
        wifi_channel += 1;
        if (jamming) {
            jam1.stopConstCarrier();
            if (g_dualRadioAvailable) jam2.stopConstCarrier();
            jam1.startConstCarrier(RF24_PA_MAX, base_rf + 10);
            if (g_dualRadioAvailable) jam2.startConstCarrier(RF24_PA_MAX, base_rf + 12);
        }
    }

    if (encOk()) {
        jamming = !jamming;
        if (jamming) {
            jam1.startConstCarrier(RF24_PA_MAX, base_rf + 10);
            if (g_dualRadioAvailable) jam2.startConstCarrier(RF24_PA_MAX, base_rf + 12);
        } else {
            jam1.stopConstCarrier();
            if (g_dualRadioAvailable) jam2.stopConstCarrier();
        }
    }

    if (encBack()) {
        jamming = false;
        jam1.stopConstCarrier();
        if (g_dualRadioAvailable) jam2.stopConstCarrier();
        runningApp = false; 
    }

    if (jamming) {
        for (int offset = 0; offset < 22; offset++) {
            int target1 = base_rf + offset;
            int target2 = base_rf + (21 - offset);

            if (target1 < 125) {
                jam1.setChannel(target1);
                jam1.writeFast(&noise, sizeof(noise));
            }
            if (g_dualRadioAvailable && target2 < 125) {
                jam2.setChannel(target2);
                jam2.writeFast(&noise, sizeof(noise));
            }
        }

        u8g2.drawBox(10, 44, 108, 16);
        u8g2.setDrawColor(0);
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(22, 56, "ATAQUE ACTIVO");
        u8g2.setDrawColor(1);
    } else {
        u8g2.drawFrame(10, 44, 108, 16);
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(25, 56, "ESTADO: LISTO");
    }

    u8g2.sendBuffer();
}

