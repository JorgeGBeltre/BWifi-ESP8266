#include "btscan.h"
#include "encoder_input.h"
#include "esp8266_compat.h"
#include <Arduino.h>
#include <RF24.h>
#include <U8g2lib.h>
#include <math.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern RF24 jam1;
extern bool runningApp;

struct BtScanEntry {
  char label[22];
};

static BtScanEntry bt_list[15];
static int bt_count = 0;
static int current_bt = 0;

static void addBtEntry(const char *str) {
  for (int i = 0; i < bt_count; i++) {
    if (strncmp(bt_list[i].label, str, 14) == 0) {
      strncpy(bt_list[i].label, str, 21);
      bt_list[i].label[21] = '\0';
      return;
    }
  }
  if (bt_count < 15) {
    strncpy(bt_list[bt_count].label, str, 21);
    bt_list[bt_count].label[21] = '\0';
    bt_count++;
  }
}

void btscanSetup() {
  bt_count = 0;
  current_bt = 0;

  BleRf24::configureRadioForBle(jam1);
  jam1.setPayloadSize(32);

  static const uint8_t rfCh[3] = {2, 26, 80};
  static const uint8_t bleCh[3] = {37, 38, 39};
  uint8_t raw[32];

  for (int step = 0; step < 30; step++) {
    encoderUpdate();
    if (encBack()) {
      jam1.stopListening();
      runningApp = false;
      return;
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawStr(2, 10, "RADAR BT/BLE ACTIVO");

    int cx = 64, cy = 38, r = 22;
    u8g2.drawCircle(cx, cy, r);
    u8g2.drawCircle(cx, cy, r / 2);
    u8g2.drawPixel(cx, cy);

    float angle = (step * 24) * 0.0174533f;
    int lx = cx + (int)(cosf(angle) * r);
    int ly = cy + (int)(sinf(angle) * r);
    u8g2.drawLine(cx, cy, lx, ly);

    if (step % 3 == 0) u8g2.drawDisc(cx + 8, cy - 5, 2);
    if (step % 5 == 0) u8g2.drawDisc(cx - 10, cy + 7, 2);

    u8g2.sendBuffer();

    // Muestrear canales BLE 37/38/39 con nRF24L01+
    for (int c = 0; c < 3; c++) {
      jam1.setChannel(rfCh[c]);
      jam1.startListening();
      delay(8);
      bool rpd = jam1.testRPD() || jam1.testCarrier();
      if (jam1.available()) {
        jam1.read(raw, 32);
        jam1.stopListening();
        for (int i = 0; i < 32; i++) raw[i] = BleRf24::swapBits(raw[i]);
        uint8_t coeff = BleRf24::swapBits(bleCh[c]) | 2;
        BleRf24::bleWhiten(raw, 32, coeff);

        uint8_t len = raw[1] & 0x3F;
        if (len >= 6 && len <= 30) {
          char entry[22];
          snprintf(entry, sizeof(entry), "%02X:%02X:%02X:%02X (%ddB)",
                   raw[7], raw[6], raw[5], raw[4], rpd ? -55 : -78);
          addBtEntry(entry);
        }
      } else {
        jam1.stopListening();
      }
    }

    // Muestrear actividad portadora en canales BT clásicos
    int btCh = 2 + ((step * 2) % 78);
    jam1.setChannel(btCh);
    jam1.startListening();
    delay(4);
    if (jam1.testRPD() || jam1.testCarrier()) {
      char entry[26];
      snprintf(entry, sizeof(entry), "BT-ACT CH%02d (-58dB)", btCh);
      addBtEntry(entry);
    }
    jam1.stopListening();
  }
  encClear();
}

void btscanLoop() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(2, 9, "BT VISTOS:");
  u8g2.setCursor(68, 9);
  u8g2.print(bt_count);
  u8g2.drawHLine(0, 11, 128);

  if (bt_count == 0) {
    u8g2.drawStr(10, 32, "Nada por aqui...");
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(10, 48, "[OK] Re-escanear");
  } else {
    int d = encStep();
    if (d != 0) {
      current_bt = (current_bt + d) % bt_count;
      if (current_bt < 0) current_bt += bt_count;
    }

    int start_idx = (current_bt / 4) * 4;
    for (int i = 0; i < 4; i++) {
      int idx = start_idx + i;
      if (idx >= bt_count) break;
      int y = 23 + (i * 11);

      if (idx == current_bt) {
        u8g2.drawRBox(2, y - 9, 124, 11, 2);
        u8g2.setDrawColor(0);
        u8g2.drawStr(6, y, bt_list[idx].label);
        u8g2.setDrawColor(1);
      } else {
        u8g2.drawStr(6, y, bt_list[idx].label);
      }
    }
  }

  if (encOk()) {
    btscanSetup();
    return;
  }

  if (encBack()) {
    jam1.stopListening();
    runningApp = false;
    encClear();
    return;
  }

  u8g2.sendBuffer();
}

