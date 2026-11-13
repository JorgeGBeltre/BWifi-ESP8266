#include "about_info.h"
#include "beacon_spam.h"
#include "ble_spam.h"
#include "ble_trackers.h"
#include "bt_jammer.h"
#include "bt_remote.h"
#include "btscan.h"
#include "centinela.h"
#include "encoder_input.h"
#include "esp8266_compat.h"
#include "evil_portal.h"
#include "games_menu.h"
#include "gui_helper.h"
#include "hybrid_attack.h"
#include "ip_scanner.h"
#include "jammer.h"
#include "log_viewer.h"
#include "packet_monitor.h"
#include "settings_menu.h"
#include "slave_control.h"
#include "snake_game.h"
#include "spectrograph.h"
#include "total_jammer.h"
#include "web_dashboard.h"
#include "web_server.h"
#include "wifi_tools.h"
#include "wifiscan.h"
#include <Arduino.h>
#include <DNSServer.h>
#include <ESP8266Ping.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <RF24.h>
#include <U8g2lib.h>
#include <Wire.h>

extern "C" {
#include <user_interface.h>
}

// --- VARIABLES DE ESTADO GLOBALES ---
bool g_dualRadioAvailable = false;
bool isTotalAttacking = false;
bool isHybridActive = false;
String target_ssid = "Ninguna";
int target_channel = 1;
String apSSID = "BW8266-ADMIN";

// OLED SSD1306 128x64 I2C en ESP8266: SCL = GPIO 5 (D1), SDA = GPIO 4 (D2)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// Radios nRF24L01+ sobre HSPI (SCK=14/D5, MISO=12/D6, MOSI=13/D7):
// Radio #1 (jam1): CE = GPIO 16 (D0), CSN = GPIO 15 (D8)
// Radio #2 (jam2, opcional): CE = GPIO 16 (D0), CSN = GPIO 10 (SD3)
RF24 jam1(16, 15);
RF24 jam2(16, 10);

ESP8266WebServer webDashServer(80);

extern uint32_t g_bootCount;

int menu_index = 0;
float currentPos = 0;
bool runningApp = false;
int TOTAL_OPTIONS = 22;

// --- ORDEN DE MENÚ v3.4 (22 MÓDULOS) ---
const char *menu_labels[] = {
    "WIFI SCANNER",    // 0
    "WIFI AUDIT PRO",  // 1
    "ANALIZADOR",      // 2
    "BT SCANNER",      // 3
    "BLE TRACKERS",    // 4
    "PACKET MONITOR",  // 5
    "MODO CENTINELA",  // 6
    "JAMMER CANAL",    // 7
    "BARRIDO TOTAL",   // 8
    "BT JAMMER",       // 9
    "BEACON SPAM",     // 10
    "BLE SPAM (POP)",  // 11
    "MODO HIBRIDO",    // 12
    "EVIL PORTAL",     // 13
    "IP SCANNER",      // 14
    "CONTROL ESCLAVO", // 15
    "WEB DASHBOARD",   // 16
    "REMOTE HID/UDP",  // 17
    "LEER LOGS",       // 18
    "ARCADE",          // 19
    "AJUSTES / TEST",  // 20
    "ABOUT"            // 21
};

void setup() {
  // Serial TX-only en GPIO 1 para dejar GPIO 3 (RX) libre para el pulsador SW
  // del encoder HW-040
  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
  g_bootCount++;

  // I2C en SDA=4 (D2), SCL=5 (D1) a 400 kHz / 800 kHz
  Wire.begin(4, 5);
  Wire.setClock(400000);
  u8g2.begin();

  encoderInit();
  settingsLoad();

  for (int i = 0; i <= 100; i += 10) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_10x20_tr);
    u8g2.drawStr(8, 28, "BWifi 8266");
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(22, 44, "MULTI-TOOL v3.5");
    u8g2.drawHLine(20, 48, 88);
    u8g2.drawFrame(14, 58, 100, 3);
    u8g2.drawBox(16, 59, (i * 96) / 100, 1);
    u8g2.sendBuffer();
    delay(15);
  }

  slaveControlSetup();
  jam1.begin();
  g_dualRadioAvailable = jam2.begin() && jam2.isChipConnected();
  settingsRunBootGuards();
  encClear();
}

void loop() {
  encoderUpdate();

  if (!runningApp) {
    if (settingsHandleScreensaver()) {
      return;
    }

    int delta = encStep();
    if (delta != 0) {
      menu_index = (menu_index + delta) % TOTAL_OPTIONS;
      if (menu_index < 0)
        menu_index += TOTAL_OPTIONS;
    }

    drawBruceMenu();

    if (encAux()) {
      saveOledScreenshotBmp("/screen.bmp");
    }

    if (encOk()) {
      encClear();
      runningApp = true;
      if (menu_index == 0)
        wifiscanSetup();
      else if (menu_index == 1)
        wifiToolsSetup();
      else if (menu_index == 2)
        spectrographSetup();
      else if (menu_index == 3)
        btscanSetup();
      else if (menu_index == 4)
        bleTrackersSetup();
      else if (menu_index == 5)
        monitorSetup();
      else if (menu_index == 7)
        jammerSetup();
      else if (menu_index == 8)
        totalJammerSetup();
      else if (menu_index == 9)
        btJammerSetup();
      else if (menu_index == 14)
        ipScannerSetup();
      else if (menu_index == 15)
        slaveControlSetup();
      else if (menu_index == 16)
        startWebServer();
      else if (menu_index == 19) {
        resetSnake();
      } else if (menu_index == 20)
        settingsSetup();
    }
  } else {
    if (menu_index == 0)
      wifiscanLoop();
    else if (menu_index == 1)
      wifiToolsLoop();
    else if (menu_index == 2)
      spectrographLoop();
    else if (menu_index == 3)
      btscanLoop();
    else if (menu_index == 4)
      bleTrackersLoop();
    else if (menu_index == 5)
      monitorLoop();
    else if (menu_index == 6)
      centinelaLoop();
    else if (menu_index == 7)
      jammerLoop();
    else if (menu_index == 8)
      totalJammerLoop();
    else if (menu_index == 9)
      btJammerLoop();
    else if (menu_index == 10)
      beaconSpamLoop();
    else if (menu_index == 11)
      bleSpamLoop();
    else if (menu_index == 12)
      hybridAttackLoop();
    else if (menu_index == 13)
      evilPortalLoop();
    else if (menu_index == 14)
      ipScannerLoop();
    else if (menu_index == 15)
      slaveControlLoop();
    else if (menu_index == 16)
      webDashboardLoop();
    else if (menu_index == 17)
      btRemoteLoop();
    else if (menu_index == 18)
      logViewerLoop();
    else if (menu_index == 19)
      gamesMenuLoop();
    else if (menu_index == 20)
      settingsLoop();
    else if (menu_index == 21)
      aboutLoop();

    if (!runningApp || (encBack() && menu_index != 14)) {
      encClear();
      runningApp = false;
      if (menu_index == 16) {
        webDashServer.stop();
        WiFi.softAPdisconnect(true);
      }
      wifi_promiscuous_enable(0);
      wifi_set_promiscuous_rx_cb(NULL);
      if (WiFi.getMode() != WIFI_OFF) {
        WiFi.mode(WIFI_OFF);
      }
      jam1.stopListening();
      jam1.stopConstCarrier();
      if (g_dualRadioAvailable) {
        jam2.stopListening();
        jam2.stopConstCarrier();
      }
    }
  }
}
