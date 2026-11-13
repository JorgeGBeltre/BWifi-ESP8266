#include <Arduino.h>
#include <U8g2lib.h>
#include "esp8266_compat.h"
#include "encoder_input.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern bool runningApp;

int logScrollY = 0;
int maxLogScroll = 0;
bool confirmErase = false; 

void logViewerLoop() {
    u8g2.clearBuffer();
    if (!SPIFFS.begin()) {
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(10, 30, "Error en LittleFS");
        u8g2.sendBuffer();
        if (encBack()) runningApp = false;
        return;
    }

    if (confirmErase) {
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawFrame(6, 8, 116, 48);
        u8g2.drawStr(12, 22, "¿BORRAR REGISTROS?");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(12, 36, "[OK]   SI, BORRAR");
        u8g2.drawStr(12, 48, "[GIRA/HOLD] CANCELAR");
        u8g2.sendBuffer();

        if (encOk()) {
            SPIFFS.remove("/logins.txt");
            logScrollY = 0;
            confirmErase = false;
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawStr(20, 35, "¡LOGS BORRADOS!");
            u8g2.sendBuffer();
            delay(900);
            encClear();
        }
        if (encStep() != 0 || encBack()) {
            confirmErase = false;
            encClear();
        }
        return;
    }

    File file = SPIFFS.open("/logins.txt", "r");
    if (!file || file.size() == 0) {
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(15, 30, "Archivo Vacio");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(15, 45, "No hay capturas aun.");
    } else {
        u8g2.setFont(u8g2_font_5x7_tr);
        int y = 22 + logScrollY;
        int totalHeight = 0;
        const int maxChars = 24; 

        while (file.available()) {
            String line = file.readStringUntil('\n');
            line.trim(); 

            while (line.length() > 0) {
                String sub = line.substring(0, min((int)line.length(), maxChars));
                if (y > 12 && y < 62) {
                    u8g2.setCursor(2, y);
                    u8g2.print(sub);
                }
                line = line.substring(sub.length());
                y += 9; 
                totalHeight += 9;
            }
        }
        file.close();

        maxLogScroll = -(totalHeight - 40);
        if (maxLogScroll > 0) maxLogScroll = 0;

        int d = encStep();
        if (d != 0) {
            logScrollY = constrain(logScrollY - (d * 10), maxLogScroll, 0);
        }

        if (encOk() || encAux()) {
            confirmErase = true;
            encClear();
        }
    }

    u8g2.setDrawColor(0);
    u8g2.drawBox(0, 0, 128, 12);
    u8g2.setDrawColor(1);
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(2, 9, "HISTORIAL");
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(62, 9, "[OK:BORRAR]");
    u8g2.drawHLine(0, 11, 128);

    if (encBack()) {
        confirmErase = false;
        runningApp = false;
        encClear();
        return;
    }

    u8g2.sendBuffer();
}

