#include "settings_menu.h"
#include "encoder_input.h"
#include "esp8266_compat.h"
#include <RF24.h>
#include <U8g2lib.h>

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern RF24 jam1;
extern RF24 jam2;
extern bool runningApp;

AppSettings g_settings = {0xB9, 3, true, 0, 0, false, false, 0, false, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

static const uint8_t kContrastTable[4] = {45, 115, 185, 255};
static const uint32_t kScreensaverMs[4] = {0, 30000, 60000, 120000};

static bool s_screenDimmed = false;
static int s_selIdx = 0;
static bool s_inSelfTest = false;
static uint32_t s_snapToastMs = 0;

void settingsApplyHardware() {
  uint8_t bIdx = (g_settings.brightnessIdx < 4) ? g_settings.brightnessIdx : 3;
  u8g2.setContrast(kContrastTable[bIdx]);
  encSetInvertDir(g_settings.invertEncoder);
  u8g2.sendF("c", (g_settings.themeMode == 1) ? 0xA7 : 0xA6);
}

void settingsLoad() {
  EEPROM.begin(128);
  AppSettings loaded;
  EEPROM.get(0, loaded);
  if (loaded.magic == 0xB9 && loaded.brightnessIdx < 4 && loaded.screensaverIdx < 4 && loaded.profileMode < 3 && loaded.themeMode < 3) {
    g_settings = loaded;
  } else {
    g_settings.magic = 0xB9;
    g_settings.brightnessIdx = 3;
    g_settings.invertEncoder = (ENC_INVERT_DIR != 0);
    g_settings.screensaverIdx = 0;
    g_settings.profileMode = 0;
    g_settings.pinEnabled = false;
    g_settings.showLegalNotice = false;
    g_settings.themeMode = 0;
    g_settings.slaveState = false;
    for (int i = 0; i < 10; i++) g_settings.arcadeHi[i] = 0;
    EEPROM.put(0, g_settings);
    EEPROM.commit();
  }
  settingsApplyHardware();
}

void settingsSave() {
  g_settings.magic = 0xB9;
  EEPROM.put(0, g_settings);
  EEPROM.commit();
  settingsApplyHardware();
}

void rotateLogIfNeeded(const char *path, size_t maxBytes) {
  if (!SPIFFS.begin()) return;
  if (!SPIFFS.exists(path)) return;
  File f = SPIFFS.open(path, "r");
  if (!f) return;
  size_t sz = f.size();
  f.close();

  if (sz >= maxBytes) {
    String bak = String(path) + ".1";
    if (SPIFFS.exists(bak)) {
      SPIFFS.remove(bak);
    }
    SPIFFS.rename(path, bak);
  }
}

bool saveOledScreenshotBmp(const char *path) {
  if (!SPIFFS.begin()) return false;
  uint8_t *buf = u8g2.getBufferPtr();
  if (!buf) return false;

  File f = SPIFFS.open(path, "w");
  if (!f) return false;

  static const uint8_t bmpHdr[62] = {
    'B', 'M',
    0x3E, 0x04, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x3E, 0x00, 0x00, 0x00,
    0x28, 0x00, 0x00, 0x00,
    0x80, 0x00, 0x00, 0x00,
    0x40, 0x00, 0x00, 0x00,
    0x01, 0x00,
    0x01, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x04, 0x00, 0x00,
    0x13, 0x0B, 0x00, 0x00,
    0x13, 0x0B, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0x00
  };
  f.write(bmpHdr, sizeof(bmpHdr));

  uint8_t rowBytes[16];
  for (int y = 63; y >= 0; y--) {
    memset(rowBytes, 0, sizeof(rowBytes));
    int page = y >> 3;
    uint8_t bitMask = (uint8_t)(1U << (y & 7));
    for (int x = 0; x < 128; x++) {
      if (buf[page * 128 + x] & bitMask) {
        rowBytes[x >> 3] |= (uint8_t)(0x80U >> (x & 7));
      }
    }
    f.write(rowBytes, sizeof(rowBytes));
  }
  f.close();
  return true;
}

void settingsRunBootGuards() {
  if (g_settings.showLegalNotice) {
    encClear();
    while (true) {
      encoderUpdate();
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tr);
      u8g2.drawRBox(0, 0, 128, 12, 2);
      u8g2.setDrawColor(0);
      u8g2.drawStr(8, 9, "AVISO USO ETICO / LAB");
      u8g2.setDrawColor(1);

      u8g2.setFont(u8g2_font_5x7_tr);
      u8g2.drawStr(2, 22, "Solo para auditoria y");
      u8g2.drawStr(2, 31, "pruebas en redes propias");
      u8g2.drawStr(2, 40, "o entorno de laboratorio.");
      u8g2.drawHLine(0, 46, 128);
      u8g2.drawStr(10, 58, "[OK] ACEPTAR Y ENTRAR");
      u8g2.sendBuffer();

      if (encOk()) {
        encClear();
        break;
      }
      delay(10);
    }
  }

  if (g_settings.pinEnabled) {
    const uint8_t targetPin[4] = {1, 2, 3, 4};
    uint8_t digits[4] = {0, 0, 0, 0};
    int pos = 0;
    bool errorFlash = false;
    uint32_t errorMs = 0;
    encClear();

    while (true) {
      encoderUpdate();
      int d = encStep();
      if (d != 0) {
        int val = ((int)digits[pos] + d) % 10;
        if (val < 0) val += 10;
        digits[pos] = (uint8_t)val;
      }

      if (encOk()) {
        pos++;
        if (pos >= 4) {
          if (digits[0] == targetPin[0] && digits[1] == targetPin[1] &&
              digits[2] == targetPin[2] && digits[3] == targetPin[3]) {
            encClear();
            break;
          } else {
            pos = 0;
            digits[0] = digits[1] = digits[2] = digits[3] = 0;
            errorFlash = true;
            errorMs = millis();
          }
        }
      }
      if (encBack() && pos > 0) {
        pos--;
      }

      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tr);
      u8g2.drawStr(14, 11, "BLOQUEO PIN (1234)");
      u8g2.drawHLine(0, 14, 128);

      for (int i = 0; i < 4; i++) {
        int bx = 18 + i * 25;
        if (i == pos) {
          u8g2.drawRBox(bx, 22, 20, 24, 3);
          u8g2.setDrawColor(0);
        } else {
          u8g2.drawRFrame(bx, 22, 20, 24, 3);
          u8g2.setDrawColor(1);
        }
        u8g2.setFont(u8g2_font_10x20_tr);
        char c[2] = {(char)('0' + digits[i]), 0};
        u8g2.drawStr(bx + 5, 41, c);
        u8g2.setDrawColor(1);
      }

      u8g2.setFont(u8g2_font_5x7_tr);
      if (errorFlash && (millis() - errorMs < 1200)) {
        u8g2.drawStr(24, 59, "! PIN INCORRECTO !");
      } else {
        errorFlash = false;
        u8g2.drawStr(8, 59, "Gira:Digito  OK:Siguiente");
      }
      u8g2.sendBuffer();
      delay(10);
    }
  }
}

bool settingsHandleScreensaver() {
  uint32_t timeout = kScreensaverMs[g_settings.screensaverIdx % 4];
  if (timeout == 0) {
    if (s_screenDimmed) {
      u8g2.setPowerSave(0);
      s_screenDimmed = false;
    }
    return false;
  }

  uint32_t idle = millis() - encLastActivityMs();
  if (idle >= timeout) {
    if (!s_screenDimmed) {
      u8g2.setPowerSave(1);
      s_screenDimmed = true;
    }
    if (encSwPressed() || encStep() != 0 || encOk() || encBack()) {
      u8g2.setPowerSave(0);
      s_screenDimmed = false;
      encTouchActivity();
      encClear();
    }
    return s_screenDimmed;
  } else if (s_screenDimmed) {
    u8g2.setPowerSave(0);
    s_screenDimmed = false;
  }
  return false;
}

static void drawSelfTest() {
  bool r1 = jam1.isChipConnected();
  bool r2 = g_dualRadioAvailable && jam2.isChipConnected();
  bool fsOk = SPIFFS.begin();
  size_t fsUsed = 0, fsTotal = 0;
  if (fsOk) {
    FSInfo info;
    if (SPIFFS.info(info)) {
      fsUsed = info.usedBytes / 1024;
      fsTotal = info.totalBytes / 1024;
    }
  }
  uint32_t freeHeapKb = ESP.getFreeHeap() / 1024;

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawRBox(0, 0, 128, 12, 2);
  u8g2.setDrawColor(0);
  u8g2.drawStr(14, 9, "SELF-TEST HARDWARE");
  u8g2.setDrawColor(1);

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setCursor(2, 22);
  u8g2.print("NRF24 #1 (D0/D8): ");
  u8g2.print(r1 ? "OK (CHIP)" : "SIN SEÑAL");

  u8g2.setCursor(2, 31);
  u8g2.print("NRF24 #2 (D0/SD3):");
  u8g2.print(r2 ? "OK (DUAL)" : "1-RADIO");

  u8g2.setCursor(2, 40);
  u8g2.print("LittleFS: ");
  if (fsOk) {
    u8g2.print(fsUsed);
    u8g2.print("/");
    u8g2.print(fsTotal);
    u8g2.print(" KB");
  } else {
    u8g2.print("ERR");
  }

  u8g2.setCursor(2, 49);
  u8g2.print("HEAP: ");
  u8g2.print(freeHeapKb);
  u8g2.print("KB | SW:");
  u8g2.print(encSwPressed() ? "ON" : "OFF");

  u8g2.drawHLine(0, 53, 128);
  u8g2.drawStr(10, 62, "[OK/HOLD] Volver Ajustes");
  u8g2.sendBuffer();
}

void settingsSetup() {
  s_selIdx = 0;
  s_inSelfTest = false;
  s_snapToastMs = 0;
  encClear();
}

void settingsLoop() {
  if (s_inSelfTest) {
    drawSelfTest();
    if (encOk() || encBack()) {
      s_inSelfTest = false;
      encClear();
    }
    return;
  }

  const int kTotalItems = 10;
  int d = encStep();
  if (d != 0) {
    s_selIdx = (s_selIdx + d) % kTotalItems;
    if (s_selIdx < 0) s_selIdx += kTotalItems;
  }

  if (encOk()) {
    switch (s_selIdx) {
    case 0:
      g_settings.brightnessIdx = (g_settings.brightnessIdx + 1) % 4;
      settingsSave();
      break;
    case 1:
      g_settings.invertEncoder = !g_settings.invertEncoder;
      settingsSave();
      break;
    case 2:
      g_settings.screensaverIdx = (g_settings.screensaverIdx + 1) % 4;
      settingsSave();
      break;
    case 3:
      g_settings.profileMode = (g_settings.profileMode + 1) % 3;
      settingsSave();
      break;
    case 4:
      g_settings.pinEnabled = !g_settings.pinEnabled;
      settingsSave();
      break;
    case 5:
      g_settings.showLegalNotice = !g_settings.showLegalNotice;
      settingsSave();
      break;
    case 6:
      g_settings.themeMode = (g_settings.themeMode + 1) % 3;
      settingsSave();
      break;
    case 7:
      saveOledScreenshotBmp("/screen.bmp");
      s_snapToastMs = millis();
      break;
    case 8:
      s_inSelfTest = true;
      break;
    case 9:
      runningApp = false;
      break;
    }
    encClear();
    return;
  }

  if (encBack()) {
    runningApp = false;
    encClear();
    return;
  }

  const char *brightNames[] = {"25%", "50%", "75%", "100%"};
  const char *ssNames[] = {"OFF", "30s", "60s", "120s"};
  const char *profNames[] = {"LAB", "AUDIT", "DEMO"};
  const char *themeNames[] = {"CLASICO", "INVERSO", "RETRO"};

  char lines[kTotalItems][26];
  snprintf(lines[0], sizeof(lines[0]), "1. BRILLO OLED: %s", brightNames[g_settings.brightnessIdx % 4]);
  snprintf(lines[1], sizeof(lines[1]), "2. GIRO ENCODER: %s", g_settings.invertEncoder ? "INV" : "NORM");
  snprintf(lines[2], sizeof(lines[2]), "3. SALVAPANTALLA: %s", ssNames[g_settings.screensaverIdx % 4]);
  snprintf(lines[3], sizeof(lines[3]), "4. PERFIL MODO: %s", profNames[g_settings.profileMode % 3]);
  snprintf(lines[4], sizeof(lines[4]), "5. PIN INICIO: %s", g_settings.pinEnabled ? "1234" : "OFF");
  snprintf(lines[5], sizeof(lines[5]), "6. AVISO LAB: %s", g_settings.showLegalNotice ? "ON" : "OFF");
  snprintf(lines[6], sizeof(lines[6]), "7. TEMA UI: %s", themeNames[g_settings.themeMode % 3]);
  snprintf(lines[7], sizeof(lines[7]), "8. CAPTURAR BMP (/screen)");
  snprintf(lines[8], sizeof(lines[8]), "9. SELF-TEST HW ->");
  snprintf(lines[9], sizeof(lines[9]), "10. <- SALIR AL MENU");

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawRBox(0, 0, 128, 12, 2);
  u8g2.setDrawColor(0);
  if (s_snapToastMs != 0 && (millis() - s_snapToastMs < 1300)) {
    u8g2.drawStr(8, 9, "BMP GUARDADO EN FS!");
  } else {
    u8g2.drawStr(12, 9, "AJUSTES & NVS (v3.5)");
  }
  u8g2.setDrawColor(1);

  int firstVis = s_selIdx - 2;
  if (firstVis > kTotalItems - 4) firstVis = kTotalItems - 4;
  if (firstVis < 0) firstVis = 0;

  u8g2.setFont(u8g2_font_5x7_tr);
  for (int row = 0; row < 4; row++) {
    int idx = firstVis + row;
    if (idx >= kTotalItems) break;
    int y = 23 + row * 12;
    if (idx == s_selIdx) {
      u8g2.drawRBox(2, y - 8, 124, 11, 2);
      u8g2.setDrawColor(0);
      u8g2.drawStr(6, y, lines[idx]);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(6, y, lines[idx]);
    }
  }

  u8g2.sendBuffer();
}
