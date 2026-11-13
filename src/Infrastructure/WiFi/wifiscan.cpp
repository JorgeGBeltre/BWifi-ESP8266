#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <U8g2lib.h>
#include "encoder_input.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern String target_ssid;
extern int target_channel;
extern bool runningApp;

int net_count = 0;
int current_net = 0;
bool show_details = false; 

int textScrollX = 0;
unsigned long lastScrollMove = 0;

void drawLockIcon(int x, int y, bool locked) {
    if (locked) {
        u8g2.drawBox(x, y + 3, 6, 5);
        u8g2.drawFrame(x + 1, y, 4, 3);
    } else {
        u8g2.drawFrame(x, y + 3, 6, 5);
        u8g2.drawLine(x + 4, y, x + 4, y + 2);
    }
}

void drawSignalBars(int x, int y, long rssi) {
    int bars = 0;
    if (rssi > -55) bars = 4;
    else if (rssi > -65) bars = 3;
    else if (rssi > -75) bars = 2;
    else if (rssi > -85) bars = 1;

    for (int i = 0; i < 4; i++) {
        int h = (i + 1) * 2;
        if (i < bars) u8g2.drawBox(x + (i * 3), y - h, 2, h);
        else u8g2.drawFrame(x + (i * 3), y - h, 2, h);
    }
}

void connectToOpenNet(String ssid, int ch) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(10, 20, "CONECTANDO A:");
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(10, 35, ssid.substring(0, 18).c_str());
    u8g2.sendBuffer();

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str()); 

    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
        delay(500);
        tries++;
        u8g2.drawBox(10 + (tries * 5), 50, 3, 4);
        u8g2.sendBuffer();
    }

    u8g2.clearBuffer();
    if (WiFi.status() == WL_CONNECTED) {
        target_ssid = ssid;
        target_channel = ch;
        u8g2.drawStr(10, 20, "!CONECTADO!");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(10, 35, "IP Asignada:");
        u8g2.setCursor(10, 48);
        u8g2.print(WiFi.localIP());
        u8g2.drawStr(10, 60, "Listo para IP SCANNER");
    } else {
        u8g2.drawStr(10, 30, "ERROR DE CONEXION");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(10, 45, "No se obtuvo IP.");
    }
    u8g2.sendBuffer();
    delay(2000);
    encClear();
}

void wifiscanSetup() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawRFrame(10, 20, 108, 26, 4);
    u8g2.drawStr(22, 36, "BUSCANDO WIFI...");
    u8g2.sendBuffer();
    
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(80);
    
    net_count = WiFi.scanNetworks();
    current_net = 0;
    show_details = false;
    textScrollX = 0;
    encClear();
}

void wifiscanLoop() {
    u8g2.clearBuffer();
    
    if (net_count <= 0) {
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(15, 30, "Sin redes cercanas");
        u8g2.drawStr(10, 50, "[OK] Re-escanear");
        if (encOk()) {
            wifiscanSetup();
        }
        if (encBack()) {
            runningApp = false;
        }
        u8g2.sendBuffer();
        return;
    }

    if (!show_details) {
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(2, 9, "REDES:");
        u8g2.setCursor(42, 9); u8g2.print(net_count);
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(66, 9, "[OK:INFO HOLD:SALIR]");
        u8g2.drawHLine(0, 11, 128);

        int d = encStep();
        if (d != 0) {
            current_net = (current_net + d) % net_count;
            if (current_net < 0) current_net += net_count;
            textScrollX = 0;
        }
        
        if (encOk()) {
            show_details = true;
            encClear();
        }

        if (encAux()) {
            target_ssid = WiFi.SSID(current_net);
            target_channel = WiFi.channel(current_net);
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawRFrame(8, 16, 112, 32, 4);
            u8g2.drawStr(22, 30, "!RED CLONADA!");
            u8g2.setFont(u8g2_font_5x7_tr);
            u8g2.drawStr(14, 42, target_ssid.substring(0, 18).c_str());
            u8g2.sendBuffer();
            delay(800);
            encClear();
            return;
        }

        if (encBack()) {
            runningApp = false;
            encClear();
            return;
        }

        int start_idx = (current_net / 4) * 4;
        for (int i = 0; i < 4; i++) {
            int idx = start_idx + i;
            if (idx >= net_count) break;
            int y = 23 + (i * 12);
            bool isSelected = (idx == current_net);

            if (isSelected) {
                u8g2.drawRBox(0, y - 9, 128, 12, 2);
                u8g2.setDrawColor(0); 
            } else {
                u8g2.setDrawColor(1);
            }

            drawLockIcon(3, y - 7, WiFi.encryptionType(idx) != ENC_TYPE_NONE);
            
            String ssid = WiFi.SSID(idx);
            u8g2.setFont(u8g2_font_6x10_tr);
            
            if (isSelected && ssid.length() > 12) {
                if (millis() - lastScrollMove > 150) {
                    textScrollX += 2;
                    if (textScrollX > (int)(ssid.length() * 6)) textScrollX = -20;
                    lastScrollMove = millis();
                }
                u8g2.setClipWindow(12, y - 9, 86, y + 3);
                u8g2.drawStr(12 - (textScrollX > 0 ? textScrollX : 0), y, ssid.c_str());
                u8g2.setMaxClipWindow();
            } else {
                if (ssid.length() > 12) ssid = ssid.substring(0, 11) + ".";
                u8g2.drawStr(12, y, ssid.c_str());
            }

            long rssi = WiFi.RSSI(idx);
            u8g2.setFont(u8g2_font_5x7_tr);
            u8g2.setCursor(90, y - 1);
            u8g2.print(rssi);
            drawSignalBars(112, y, rssi);
            u8g2.setDrawColor(1); 
        }

    } else {
        bool isOpen = (WiFi.encryptionType(current_net) == ENC_TYPE_NONE);
        
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(0, 10, "DETALLES DE RED:");
        u8g2.drawHLine(0, 12, 128);

        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.setCursor(0, 22); u8g2.print("SSID: "); u8g2.print(WiFi.SSID(current_net).substring(0, 18));
        u8g2.setCursor(0, 31); u8g2.print("MAC:  "); u8g2.print(WiFi.BSSIDstr(current_net));
        u8g2.setCursor(0, 40); u8g2.print("CH:   "); u8g2.print(WiFi.channel(current_net));
        u8g2.print(" | RSSI: "); u8g2.print(WiFi.RSSI(current_net)); u8g2.print("dBm");
        u8g2.setCursor(0, 49); u8g2.print("SEG:  "); 
        u8g2.print(isOpen ? "ABIERTA" : "PROTEGIDA (WPA)");

        u8g2.drawBox(0, 54, 128, 10);
        u8g2.setDrawColor(0);
        if (isOpen) {
            u8g2.drawStr(2, 62, "[OK] CONECTAR [HOLD] ATRAS");
        } else {
            u8g2.drawStr(2, 62, "[OK] CLONAR   [HOLD] ATRAS");
        }
        u8g2.setDrawColor(1);

        if (encOk()) {
            if (isOpen) {
                connectToOpenNet(WiFi.SSID(current_net), WiFi.channel(current_net));
                show_details = false;
            } else {
                target_ssid = WiFi.SSID(current_net);
                target_channel = WiFi.channel(current_net);
                
                u8g2.clearBuffer();
                u8g2.setFont(u8g2_font_6x10_tr);
                u8g2.drawStr(25, 30, "!RED CLONADA!");
                u8g2.drawStr(15, 45, "Lista para Ataque");
                u8g2.sendBuffer();
                delay(900);
                show_details = false; 
            }
            encClear();
        }

        if (encBack()) {
            show_details = false;
            encClear();
        }
    }
    u8g2.sendBuffer();
}

