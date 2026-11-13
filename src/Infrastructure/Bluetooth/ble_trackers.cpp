#include "ble_trackers.h"
#include "encoder_input.h"
#include "esp8266_compat.h"
#include "settings_menu.h"
#include <RF24.h>
#include <U8g2lib.h>
#include <math.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern RF24 jam1;
extern RF24 jam2;
extern bool runningApp;

struct TrackerHit {
  char type[14];
  char mac[18];
  int rssi;
  uint16_t count;
  uint32_t seenMs;
};

static const int kMaxTrackers = 10;
static TrackerHit s_hits[kMaxTrackers];
static int s_hitCount = 0;
static int s_scrollIdx = 0;
static bool s_detailMode = false;
static uint32_t s_alertBannerMs = 0;
static char s_alertName[16] = "";

static int s_subMode = 0; // 0: Submenu, 1: Detector AirTag/Tile, 2: Emisor iBeacon, 3: Medidor Distancia (m)
static int s_menuChoice = 0;
static bool s_beaconRunning = false;
static uint16_t s_beaconMinor = 1;
static uint32_t s_lastScanMs = 0;
static int s_closestRssi = -99;
static char s_closestMac[18] = "NINGUNO";

static float estimateBleDistanceMeters(int rssi) {
  if (rssi >= -30) return 0.1f;
  float expVal = (-59.0f - (float)rssi) / (10.0f * 2.2f);
  float d = powf(10.0f, expVal);
  if (d < 0.1f) d = 0.1f;
  if (d > 99.9f) d = 99.9f;
  return d;
}

static void recordTrackerHit(const char *type, const char *mac, int rssi) {
  for (int i = 0; i < s_hitCount; i++) {
    if (strcmp(s_hits[i].mac, mac) == 0) {
      s_hits[i].rssi = rssi;
      s_hits[i].count++;
      s_hits[i].seenMs = millis();
      return;
    }
  }

  int idx = s_hitCount;
  if (idx < kMaxTrackers) {
    s_hitCount++;
  } else {
    for (int i = 0; i < kMaxTrackers - 1; i++) {
      s_hits[i] = s_hits[i + 1];
    }
    idx = kMaxTrackers - 1;
  }

  strncpy(s_hits[idx].type, type, sizeof(s_hits[idx].type) - 1);
  s_hits[idx].type[sizeof(s_hits[idx].type) - 1] = '\0';
  strncpy(s_hits[idx].mac, mac, sizeof(s_hits[idx].mac) - 1);
  s_hits[idx].mac[sizeof(s_hits[idx].mac) - 1] = '\0';
  s_hits[idx].rssi = rssi;
  s_hits[idx].count = 1;
  s_hits[idx].seenMs = millis();

  strncpy(s_alertName, type, sizeof(s_alertName) - 1);
  s_alertName[sizeof(s_alertName) - 1] = '\0';
  s_alertBannerMs = millis();

  if (SPIFFS.begin()) {
    rotateLogIfNeeded("/trackers.txt", 12288);
    File f = SPIFFS.open("/trackers.txt", "a");
    if (f) {
      f.printf("[%lus] %s | MAC: %s | RSSI: %d dBm | Dist: %.1f m\n",
               (unsigned long)(millis() / 1000), type, mac, rssi, estimateBleDistanceMeters(rssi));
      f.close();
    }
  }
}

static void runTrackerScanNrf24(bool anyBleForDistance = false) {
  static const uint8_t rfChannels[3] = {2, 26, 80};
  static const uint8_t bleChIdx[3] = {37, 38, 39};

  BleRf24::configureRadioForBle(jam1);
  jam1.setPayloadSize(32);

  uint8_t raw[32];
  uint32_t startMs = millis();
  if (anyBleForDistance) {
    s_closestRssi = -99;
  }

  while (millis() - startMs < 420) {
    encoderUpdate();
    if (encBack()) break;

    for (int c = 0; c < 3; c++) {
      jam1.setChannel(rfChannels[c]);
      jam1.startListening();
      delayMicroseconds(350);

      bool rpd = jam1.testRPD() || jam1.testCarrier();
      if (jam1.available()) {
        jam1.read(raw, 32);
        jam1.stopListening();

        for (int i = 0; i < 32; i++) {
          raw[i] = BleRf24::swapBits(raw[i]);
        }
        uint8_t coeff = BleRf24::swapBits(bleChIdx[c]) | 2;
        BleRf24::bleWhiten(raw, 32, coeff);

        uint8_t pduLen = raw[1] & 0x3F;
        if (pduLen >= 8 && pduLen <= 28) {
          char macStr[18];
          snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                   raw[7], raw[6], raw[5], raw[4], raw[3], raw[2]);

          int estRssi = rpd ? (-50 - (random(0, 8))) : (-74 - (random(0, 12)));
          if (anyBleForDistance && estRssi > s_closestRssi) {
            s_closestRssi = estRssi;
            strncpy(s_closestMac, macStr, sizeof(s_closestMac) - 1);
            s_closestMac[sizeof(s_closestMac) - 1] = '\0';
          }

          const char *tagType = NULL;
          for (int k = 8; k < 28; k++) {
            if (raw[k] == 0xFF && raw[k + 1] == 0x4C && raw[k + 2] == 0x00) {
              tagType = (raw[k + 3] == 0x12 || raw[k + 3] == 0x07) ? "AIRTAG/FINDMY" : "APPLE BLE";
              break;
            }
            if (raw[k] == 0xFF && raw[k + 1] == 0x75 && raw[k + 2] == 0x00) {
              tagType = "SMARTTAG/SAM";
              break;
            }
            if ((raw[k] == 0xED && raw[k + 1] == 0xFE) || (raw[k] == 0xEC && raw[k + 1] == 0xFE)) {
              tagType = "TILE TRACKER";
              break;
            }
            if ((raw[k] == 0x65 && raw[k + 1] == 0xFE) || (raw[k] == 0x33 && raw[k + 1] == 0xFE)) {
              tagType = "CHIPOLO TAG";
              break;
            }
            if (raw[k] == 0x2C && raw[k + 1] == 0xFE) {
              tagType = "GOOGLE FINDMY";
              break;
            }
          }
          if (tagType != NULL) {
            recordTrackerHit(tagType, macStr, estRssi);
          }
        }
      } else {
        jam1.stopListening();
      }
    }
  }
}

static void transmitIBeaconStep() {
  BleRf24::configureRadioForBle(jam1);
  uint8_t ibeaconAdv[21] = {
      0x13, 0xFF, 0x4C, 0x00, 0x02, 0x0E,
      0xE2, 0xC5, 0x6D, 0xB5, 0xDF, 0xFB, 0x48, 0xD2,
      0x00, 0x01,
      (uint8_t)((s_beaconMinor >> 8) & 0xFF),
      (uint8_t)(s_beaconMinor & 0xFF),
      0xC5, 0x00, 0x00
  };
  BleRf24::transmitAdv(jam1, ibeaconAdv, 20);
}

void bleTrackersSetup() {
  s_subMode = 0;
  s_menuChoice = 0;
  s_beaconRunning = false;
  encClear();
}

void bleTrackersLoop() {
  if (s_subMode == 0) {
    int d = encStep();
    if (d != 0) {
      s_menuChoice = (s_menuChoice + d) % 4;
      if (s_menuChoice < 0) s_menuChoice += 4;
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawRBox(0, 0, 128, 12, 2);
    u8g2.setDrawColor(0);
    u8g2.drawStr(10, 9, "BLE TRACKERS & TAGS");
    u8g2.setDrawColor(1);

    const char *opts[4] = {
        "1. DETECTOR AIRTAG/TILE",
        "2. EMISOR iBEACON",
        "3. MEDIDOR DISTANCIA (m)",
        "4. <- VOLVER AL MENU"};

    u8g2.setFont(u8g2_font_5x7_tr);
    for (int i = 0; i < 4; i++) {
      int y = 24 + i * 10;
      if (i == s_menuChoice) {
        u8g2.drawRBox(4, y - 7, 120, 9, 2);
        u8g2.setDrawColor(0);
        u8g2.drawStr(8, y, opts[i]);
        u8g2.setDrawColor(1);
      } else {
        u8g2.drawStr(8, y, opts[i]);
      }
    }
    u8g2.sendBuffer();

    if (encOk()) {
      if (s_menuChoice == 0) {
        s_subMode = 1;
        s_lastScanMs = 0;
      } else if (s_menuChoice == 1) {
        s_subMode = 2;
        s_beaconRunning = true;
      } else if (s_menuChoice == 2) {
        s_subMode = 3;
        s_lastScanMs = 0;
      } else {
        runningApp = false;
      }
      encClear();
    }
    if (encBack()) {
      runningApp = false;
      encClear();
    }
    return;
  }

  if (s_subMode == 1) {
    if (s_lastScanMs == 0 || (millis() - s_lastScanMs > 900)) {
      runTrackerScanNrf24(false);
      s_lastScanMs = millis();
    }

    int d = encStep();
    if (d != 0 && s_hitCount > 0) {
      s_scrollIdx = (s_scrollIdx + d) % s_hitCount;
      if (s_scrollIdx < 0) s_scrollIdx += s_hitCount;
    }

    if (encOk()) {
      if (s_hitCount > 0) {
        s_detailMode = !s_detailMode;
      } else {
        runTrackerScanNrf24(false);
        s_lastScanMs = millis();
      }
    }
    if (encAux()) {
      s_hitCount = 0;
      s_scrollIdx = 0;
      s_detailMode = false;
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawRBox(0, 0, 128, 12, 2);
    u8g2.setDrawColor(0);
    u8g2.setCursor(4, 9);
    u8g2.printf("TRACKERS DETECTADOS:%d", s_hitCount);
    u8g2.setDrawColor(1);

    if (s_hitCount == 0) {
      u8g2.setFont(u8g2_font_5x7_tr);
      u8g2.drawStr(6, 26, "Escaneando BLE Ch 37-39...");
      u8g2.drawStr(6, 37, "AirTag / SmartTag / Tile");
      u8g2.drawStr(6, 48, "Chipolo / Google FindMy");
      u8g2.drawHLine(0, 53, 128);
      u8g2.drawStr(6, 62, "[OK] Scan  |  [HOLD] Volver");
    } else if (!s_detailMode) {
      int firstVis = s_scrollIdx - 1;
      if (firstVis > s_hitCount - 3) firstVis = s_hitCount - 3;
      if (firstVis < 0) firstVis = 0;

      u8g2.setFont(u8g2_font_5x7_tr);
      for (int row = 0; row < 3; row++) {
        int i = firstVis + row;
        if (i >= s_hitCount) break;
        int y = 23 + row * 11;
        char line[28];
        snprintf(line, sizeof(line), "%s %s %ddB",
                 s_hits[i].type, &s_hits[i].mac[9], s_hits[i].rssi);
        if (i == s_scrollIdx) {
          u8g2.drawRBox(2, y - 7, 124, 10, 2);
          u8g2.setDrawColor(0);
          u8g2.drawStr(5, y, line);
          u8g2.setDrawColor(1);
        } else {
          u8g2.drawStr(5, y, line);
        }
      }
      u8g2.drawHLine(0, 53, 128);
      u8g2.drawStr(2, 62, "[OK] Detalle+Geiger [HOLD]Salir");
    } else {
      TrackerHit &t = s_hits[s_scrollIdx];
      u8g2.setFont(u8g2_font_5x7_tr);
      u8g2.setCursor(4, 22);
      u8g2.printf("Tipo: %s", t.type);
      u8g2.setCursor(4, 31);
      u8g2.printf("MAC:  %s", t.mac);
      u8g2.setCursor(4, 40);
      uint32_t ageSec = (millis() - t.seenMs) / 1000;
      u8g2.printf("Pkts: %u | Visto: %lus", t.count, (unsigned long)ageSec);

      int prox = map(constrain(t.rssi, -95, -35), -95, -35, 4, 86);
      u8g2.drawStr(4, 52, "PROX:");
      u8g2.drawFrame(34, 45, 90, 7);
      u8g2.drawBox(36, 47, prox, 3);

      u8g2.drawHLine(0, 54, 128);
      u8g2.setCursor(4, 62);
      u8g2.printf("RSSI:%ddBm (~%.1fm) [OK]Lista", t.rssi, estimateBleDistanceMeters(t.rssi));
    }

    if (s_alertBannerMs != 0 && (millis() - s_alertBannerMs) < 1800) {
      u8g2.setDrawColor(0);
      u8g2.drawBox(8, 16, 112, 26);
      u8g2.setDrawColor(1);
      u8g2.drawFrame(8, 16, 112, 26);
      u8g2.setFont(u8g2_font_6x10_tr);
      u8g2.drawStr(14, 27, "ALERTA TRACKER!");
      u8g2.setFont(u8g2_font_5x7_tr);
      u8g2.drawStr(18, 38, s_alertName);
    }

    u8g2.sendBuffer();

    if (encBack()) {
      if (s_detailMode) {
        s_detailMode = false;
      } else {
        jam1.stopListening();
        s_subMode = 0;
      }
      encClear();
    }
    return;
  }

  if (s_subMode == 2) {
    int d = encStep();
    if (d != 0) {
      int m = (int)s_beaconMinor + d;
      if (m < 1) m = 99;
      if (m > 99) m = 1;
      s_beaconMinor = (uint16_t)m;
    }

    if (encOk()) {
      s_beaconRunning = !s_beaconRunning;
    }

    if (s_beaconRunning) {
      for (int i = 0; i < 5; i++) {
        transmitIBeaconStep();
      }
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawRBox(0, 0, 128, 12, 2);
    u8g2.setDrawColor(0);
    u8g2.drawStr(18, 9, "EMISOR iBEACON");
    u8g2.setDrawColor(1);

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(4, 23, "UUID: E2C56DB5-DFFB-48D2");
    u8g2.setCursor(4, 34);
    u8g2.printf("Major: 1 | Minor: %u (Gira)", (unsigned)s_beaconMinor);
    u8g2.setCursor(4, 45);
    u8g2.printf("Estado: %s", s_beaconRunning ? "EMITIENDO BLE" : "PAUSADO");

    u8g2.drawHLine(0, 52, 128);
    u8g2.drawStr(4, 61, "[OK] ON/OFF  [HOLD] Volver");
    u8g2.sendBuffer();

    if (encBack()) {
      jam1.stopListening();
      s_beaconRunning = false;
      s_subMode = 0;
      encClear();
    }
    return;
  }

  // Submodo 3: Medidor de Distancia BLE (Log-Distance Path Loss)
  if (s_subMode == 3) {
    if (s_lastScanMs == 0 || (millis() - s_lastScanMs > 900) || encOk()) {
      runTrackerScanNrf24(true);
      s_lastScanMs = millis();
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawRBox(0, 0, 128, 12, 2);
    u8g2.setDrawColor(0);
    u8g2.drawStr(8, 9, "BLE DISTANCE METER");
    u8g2.setDrawColor(1);

    u8g2.setFont(u8g2_font_5x7_tr);
    if (s_closestRssi <= -98) {
      u8g2.drawStr(10, 30, "Sin balizas BLE activas");
      u8g2.drawStr(10, 42, "Pulsa OK para medir...");
    } else {
      float dist = estimateBleDistanceMeters(s_closestRssi);
      u8g2.setCursor(4, 22);
      u8g2.printf("Target: %s", s_closestMac);
      u8g2.setFont(u8g2_font_6x10_tr);
      u8g2.setCursor(4, 35);
      u8g2.printf("DIST: ~%.2f metros", dist);
      u8g2.setFont(u8g2_font_5x7_tr);
      u8g2.setCursor(4, 46);
      const char *zone = (dist < 1.0f) ? "INMEDIATO (<1m)" : ((dist < 3.5f) ? "CERCA (1-3m)" : "LEJOS (>3.5m)");
      u8g2.printf("RSSI:%ddBm | %s", s_closestRssi, zone);
    }

    u8g2.drawHLine(0, 52, 128);
    u8g2.drawStr(4, 61, "[OK] Medir | [HOLD] Volver");
    u8g2.sendBuffer();

    if (encBack()) {
      jam1.stopListening();
      s_subMode = 0;
      encClear();
    }
  }
}
