#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <U8g2lib.h>
#include "encoder_input.h"

extern "C" {
#include <user_interface.h>
}

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern bool runningApp;

const char* ssids[] = {
    "FREE_INTERNET", "FBI_SURVEILLANCE", "Virus_Detected!", 
    "No_Te_Conectes", "Hackeando_Red...", "Skynet_Global",
    "Cargando_Troyano", "WiFi_Gratis_Aqui", "Error_404_WiFi",
    "Red_Oculta_NSA", "Tu_Vecino_Te_Espia", "Cuidado_Con_El_Perro"
};
const int num_ssids = 12;
bool isBeaconSpamming = false;

uint8_t packet[128] = { 
    0x80, 0x00, 0x00, 0x00, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 
    0xc0, 0x6c, 
    0x83, 0x51, 0xf7, 0x8f, 0x0f, 0x00, 0x00, 0x00, 
    0x64, 0x00, 
    0x11, 0x04, 
    0x00 
};

void beaconSpamSetup() {
    isBeaconSpamming = false;
    WiFi.mode(WIFI_STA);
    wifi_set_opmode(STATION_MODE);
    wifi_promiscuous_enable(1);
}

void sendBeacon(const char* ssid) {
    int ssid_len = strlen(ssid);
    if (ssid_len > 32) ssid_len = 32;

    uint8_t channel = random(1, 12);
    wifi_set_channel(channel);

    for (int i = 10; i < 16; i++) {
        uint8_t r = random(256);
        packet[i] = packet[i + 6] = r;
    }

    packet[37] = ssid_len;
    for (int i = 0; i < ssid_len; i++) {
        packet[38 + i] = ssid[i];
    }

    int pos = 38 + ssid_len;
    uint8_t post_ssid[] = {
        0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c,
        0x03, 0x01, channel,
        0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 
        0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 
        0x00, 0x00
    };

    memcpy(&packet[pos], post_ssid, sizeof(post_ssid));
    int total_size = pos + sizeof(post_ssid);

    wifi_send_pkt_freedom(packet, total_size, 0);
}

void drawBeaconUI() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(25, 12, "BEACON SPAM");
    u8g2.drawHLine(0, 15, 128);

    if (isBeaconSpamming) {
        u8g2.drawBox(10, 22, 108, 20);
        u8g2.setDrawColor(0);
        u8g2.drawStr(20, 36, "SPAM ACTIVO...");
        u8g2.setDrawColor(1);
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(15, 52, "Clonando 12 Redes");
        u8g2.drawStr(15, 61, "[OK/HOLD] DETENER");
    } else {
        u8g2.drawFrame(10, 22, 108, 20);
        u8g2.drawStr(28, 36, "MODO ESPERA");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(10, 52, "Usa: Antena Interna");
        u8g2.drawStr(10, 61, "[OK] INICIAR [HOLD] SALIR");
    }
    u8g2.sendBuffer();
}

void beaconSpamLoop() {
    if (encOk()) {
        isBeaconSpamming = !isBeaconSpamming;
        if (isBeaconSpamming) beaconSpamSetup();
        else wifi_promiscuous_enable(0);
        drawBeaconUI();
    }

    if (encBack()) {
        isBeaconSpamming = false;
        wifi_promiscuous_enable(0);
        WiFi.mode(WIFI_OFF);
        runningApp = false;
        return;
    }

    if (isBeaconSpamming) {
        for (int i = 0; i < num_ssids; i++) {
            sendBeacon(ssids[i]);
            delay(2); 
        }
    } else {
        static unsigned long lastDraw = 0;
        if (millis() - lastDraw > 250) {
            drawBeaconUI();
            lastDraw = millis();
        }
    }
}

