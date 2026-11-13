#include "wifi_tools.h"
#include "encoder_input.h"
#include "esp8266_compat.h"
#include "settings_menu.h"
#include <ESP8266WiFi.h>
#include <U8g2lib.h>
#include <time.h>

extern "C" {
#include <user_interface.h>
}

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern bool runningApp;

struct ProbeEntry {
  uint8_t mac[6];
  char ssid[21];
  int8_t rssi;
  uint8_t ch;
  bool isRandomMac;
};

struct HiddenNetEntry {
  uint8_t bssid[6];
  char ssid[21];
  uint8_t ch;
  int8_t rssi;
  bool unmasked;
};

struct KarmaEntry {
  uint8_t bssid[6];
  char lastSsids[3][15];
  uint8_t distinctCount;
  int8_t rssi;
};

struct RogueAlert {
  char ssid[21];
  uint8_t bssid1[6];
  uint8_t bssid2[6];
  uint8_t ch1;
  uint8_t ch2;
  bool encMismatch;
};

static const int kMaxProbes = 12;
static ProbeEntry s_probes[kMaxProbes];
static volatile int s_probeCount = 0;
static volatile uint32_t s_totalProbePkts = 0;
static volatile uint32_t s_rndMacCount = 0;
static volatile uint32_t s_realMacCount = 0;

static const int kMaxHidden = 8;
static HiddenNetEntry s_hidden[kMaxHidden];
static volatile int s_hiddenCount = 0;
static volatile int s_unmaskedTotal = 0;

static const int kMaxKarma = 6;
static KarmaEntry s_karma[kMaxKarma];
static volatile int s_karmaCount = 0;
static volatile int s_karmaSuspects = 0;

static const int kMaxRogues = 6;
static RogueAlert s_rogues[kMaxRogues];
static int s_rogueCount = 0;
static int s_rogueScroll = 0;
static uint32_t s_lastRogueScanMs = 0;
static int s_totalApsLastScan = 0;

static volatile uint32_t s_pcapPackets = 0;
static volatile uint32_t s_pcapBytes = 0;
static uint8_t s_pcapRing[4][72];
static uint8_t s_pcapRingLen[4];
static volatile uint8_t s_pcapHead = 0;
static volatile uint8_t s_pcapTail = 0;

static int s_subMode = 0; // 0: Menu, 1..7 submodes
static int s_menuIdx = 0;
static int s_probeScroll = 0;
static int s_currentCh = 1;
static uint32_t s_lastHopMs = 0;

static uint8_t s_chApCount[14];
static int8_t s_chBestRssi[14];
static uint8_t s_chPeakScore[14];
static int s_bestFreeCh = 1;
static int s_busiestCh = 6;
static uint32_t s_lastHeatmapScanMs = 0;

static uint32_t s_wardriveTotalLogged = 0;
static uint32_t s_wardriveScans = 0;
static uint32_t s_lastWardriveScanMs = 0;
static uint8_t s_seenBssids[24][6];
static int s_seenBssidCount = 0;

struct RxControl8266 {
  signed rssi : 8;
  unsigned rate : 4;
  unsigned is_group : 1;
  unsigned : 1;
  unsigned sig_mode : 2;
  unsigned legacy_length : 12;
  unsigned damatch0 : 1;
  unsigned damatch1 : 1;
  unsigned bssidmatch0 : 1;
  unsigned bssidmatch1 : 1;
  unsigned MCS : 7;
  unsigned CWB : 1;
  unsigned HT_length : 16;
  unsigned Smoothing : 1;
  unsigned Not_Sounding : 1;
  unsigned : 1;
  unsigned Aggregation : 1;
  unsigned STBC : 2;
  unsigned FEC_CODING : 1;
  unsigned SGI : 1;
  unsigned rxend_state : 8;
  unsigned ampdu_cnt : 8;
  unsigned channel : 4;
  unsigned : 12;
};

struct SnifferPacket8266 {
  struct RxControl8266 rx_ctrl;
  uint8_t payload[112];
  uint16_t cnt;
  uint16_t len;
};

static void extractTagSsid(const uint8_t *tags, int maxLen, char *outSsid, int outMax) {
  outSsid[0] = '\0';
  int pos = 0;
  while (pos + 2 <= maxLen) {
    uint8_t tagId = tags[pos];
    uint8_t tagLen = tags[pos + 1];
    if (pos + 2 + tagLen > maxLen) break;
    if (tagId == 0) {
      if (tagLen > 0 && tagLen <= 32) {
        int copyLen = (tagLen >= outMax) ? (outMax - 1) : tagLen;
        bool allZero = true;
        for (int i = 0; i < copyLen; i++) {
          uint8_t b = tags[pos + 2 + i];
          if (b != 0) allZero = false;
          outSsid[i] = (b >= 32 && b <= 126) ? (char)b : '?';
        }
        outSsid[copyLen] = '\0';
        if (allZero) outSsid[0] = '\0';
      }
      return;
    }
    pos += 2 + tagLen;
  }
}

static void IRAM_ATTR wifiPromiscCb8266(uint8_t *buf, uint16_t len) {
  if (len <= sizeof(RxControl8266)) return;
  const SnifferPacket8266 *pkt = (const SnifferPacket8266 *)buf;
  const uint8_t *frame = pkt->payload;
  uint8_t subtype = frame[0] & 0xFC;
  int8_t rssi = pkt->rx_ctrl.rssi;
  uint8_t ch = pkt->rx_ctrl.channel ? pkt->rx_ctrl.channel : s_currentCh;

  // Modo 7: PCAP ring buffer
  if (s_subMode == 7) {
    uint8_t nextHead = (s_pcapHead + 1) & 3;
    if (nextHead != s_pcapTail) {
      uint8_t capLen = 64;
      memcpy(s_pcapRing[s_pcapHead], frame, capLen);
      s_pcapRingLen[s_pcapHead] = capLen;
      s_pcapHead = nextHead;
    }
    return;
  }

  // Modo 1: Probe Request + MAC Randomizer
  if (s_subMode == 1 && subtype == 0x40) {
    s_totalProbePkts++;
    const uint8_t *srcMac = &frame[10];
    bool isRnd = (srcMac[0] & 0x02) != 0;

    char parsedSsid[21];
    extractTagSsid(&frame[24], 64, parsedSsid, sizeof(parsedSsid));
    if (parsedSsid[0] == '\0') strcpy(parsedSsid, "<Broadcast>");

    for (int i = 0; i < s_probeCount; i++) {
      if (memcmp((const void *)s_probes[i].mac, srcMac, 6) == 0) {
        if (parsedSsid[0] != '<') {
          strncpy(s_probes[i].ssid, parsedSsid, 20);
          s_probes[i].ssid[20] = '\0';
        }
        s_probes[i].rssi = rssi;
        s_probes[i].ch = ch;
        return;
      }
    }

    if (isRnd) s_rndMacCount++;
    else s_realMacCount++;

    int idx = (s_probeCount < kMaxProbes) ? s_probeCount++ : (kMaxProbes - 1);
    if (idx == kMaxProbes - 1 && s_probeCount == kMaxProbes) {
      for (int i = 0; i < kMaxProbes - 1; i++) s_probes[i] = s_probes[i + 1];
    }
    memcpy(s_probes[idx].mac, srcMac, 6);
    strncpy(s_probes[idx].ssid, parsedSsid, 20);
    s_probes[idx].ssid[20] = '\0';
    s_probes[idx].rssi = rssi;
    s_probes[idx].ch = ch;
    s_probes[idx].isRandomMac = isRnd;
    return;
  }

  // Modo 5: Hidden SSID Unmasker (Beacon 0x80, ProbeResp 0x50, AssocReq 0x00)
  if (s_subMode == 5) {
    if (subtype == 0x80) {
      const uint8_t *bssid = &frame[16];
      char ssid[21];
      extractTagSsid(&frame[36], 64, ssid, sizeof(ssid));
      if (ssid[0] == '\0') {
        for (int i = 0; i < s_hiddenCount; i++) {
          if (memcmp(s_hidden[i].bssid, bssid, 6) == 0) {
            s_hidden[i].rssi = rssi;
            return;
          }
        }
        if (s_hiddenCount < kMaxHidden) {
          int idx = s_hiddenCount++;
          memcpy(s_hidden[idx].bssid, bssid, 6);
          strcpy(s_hidden[idx].ssid, "<OCULTO>");
          s_hidden[idx].ch = ch;
          s_hidden[idx].rssi = rssi;
          s_hidden[idx].unmasked = false;
        }
      }
    } else if (subtype == 0x50 || subtype == 0x00) {
      const uint8_t *bssid = &frame[16];
      int tagStart = (subtype == 0x50) ? 36 : 28;
      char ssid[21];
      extractTagSsid(&frame[tagStart], 64, ssid, sizeof(ssid));
      if (ssid[0] != '\0') {
        for (int i = 0; i < s_hiddenCount; i++) {
          if (memcmp(s_hidden[i].bssid, bssid, 6) == 0) {
            if (!s_hidden[i].unmasked) {
              s_unmaskedTotal++;
              s_hidden[i].unmasked = true;
            }
            strncpy(s_hidden[i].ssid, ssid, 20);
            s_hidden[i].ssid[20] = '\0';
            s_hidden[i].rssi = rssi;
            return;
          }
        }
        if (s_hiddenCount < kMaxHidden) {
          int idx = s_hiddenCount++;
          memcpy(s_hidden[idx].bssid, bssid, 6);
          strncpy(s_hidden[idx].ssid, ssid, 20);
          s_hidden[idx].ssid[20] = '\0';
          s_hidden[idx].ch = ch;
          s_hidden[idx].rssi = rssi;
          s_hidden[idx].unmasked = true;
          s_unmaskedTotal++;
        }
      }
    }
    return;
  }

  // Modo 6: Karma / Pineapple Detector (Probe Response 0x50)
  if (s_subMode == 6 && subtype == 0x50) {
    const uint8_t *bssid = &frame[16];
    char ssid[15];
    extractTagSsid(&frame[36], 64, ssid, sizeof(ssid));
    if (ssid[0] == '\0') return;

    for (int i = 0; i < s_karmaCount; i++) {
      if (memcmp(s_karma[i].bssid, bssid, 6) == 0) {
        s_karma[i].rssi = rssi;
        for (int j = 0; j < s_karma[i].distinctCount && j < 3; j++) {
          if (strcmp(s_karma[i].lastSsids[j], ssid) == 0) return;
        }
        if (s_karma[i].distinctCount < 3) {
          strncpy(s_karma[i].lastSsids[s_karma[i].distinctCount], ssid, 14);
          s_karma[i].lastSsids[s_karma[i].distinctCount][14] = '\0';
        }
        s_karma[i].distinctCount++;
        if (s_karma[i].distinctCount == 3) {
          s_karmaSuspects++;
        }
        return;
      }
    }

    if (s_karmaCount < kMaxKarma) {
      int idx = s_karmaCount++;
      memcpy(s_karma[idx].bssid, bssid, 6);
      strncpy(s_karma[idx].lastSsids[0], ssid, 14);
      s_karma[idx].lastSsids[0][14] = '\0';
      s_karma[idx].distinctCount = 1;
      s_karma[idx].rssi = rssi;
    }
  }
}

static void startSnifferMode(int mode) {
  s_subMode = mode;
  s_probeScroll = 0;
  s_currentCh = 1;
  s_lastHopMs = millis();

  if (mode == 1) {
    s_probeCount = 0;
    s_totalProbePkts = 0;
    s_rndMacCount = 0;
    s_realMacCount = 0;
  } else if (mode == 5) {
    s_hiddenCount = 0;
    s_unmaskedTotal = 0;
  } else if (mode == 6) {
    s_karmaCount = 0;
    s_karmaSuspects = 0;
  } else if (mode == 7) {
    s_pcapPackets = 0;
    s_pcapBytes = 24;
    s_pcapHead = 0;
    s_pcapTail = 0;
    if (SPIFFS.begin()) {
      File f = SPIFFS.open("/capture.pcap", "w");
      if (f) {
        const uint8_t pcapGlobalHdr[24] = {
          0xD4, 0xC3, 0xB2, 0xA1,
          0x02, 0x00, 0x04, 0x00,
          0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00,
          0x80, 0x00, 0x00, 0x00,
          0x69, 0x00, 0x00, 0x00
        };
        f.write(pcapGlobalHdr, 24);
        f.close();
      }
    }
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(20);
  wifi_set_opmode(STATION_MODE);
  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(wifiPromiscCb8266);
  wifi_set_channel(s_currentCh);
  wifi_promiscuous_enable(1);
}

static void stopSnifferMode() {
  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(NULL);
}

static void flushPcapRingToSpiffs() {
  if (s_pcapTail == s_pcapHead) return;
  if (!SPIFFS.begin()) return;
  if (s_pcapBytes > 16384) {
    rotateLogIfNeeded("/capture.pcap", 16384);
    File f = SPIFFS.open("/capture.pcap", "w");
    if (f) {
      const uint8_t pcapGlobalHdr[24] = {
        0xD4, 0xC3, 0xB2, 0xA1, 0x02, 0x00, 0x04, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x80, 0x00, 0x00, 0x00, 0x69, 0x00, 0x00, 0x00
      };
      f.write(pcapGlobalHdr, 24);
      f.close();
      s_pcapBytes = 24;
    }
  }

  File f = SPIFFS.open("/capture.pcap", "a");
  if (!f) return;

  while (s_pcapTail != s_pcapHead) {
    uint8_t idx = s_pcapTail;
    uint32_t capLen = s_pcapRingLen[idx];
    uint32_t tsSec = millis() / 1000;
    uint32_t tsUsec = (millis() % 1000) * 1000;
    uint32_t pktHdr[4] = {tsSec, tsUsec, capLen, capLen};
    f.write((const uint8_t *)pktHdr, 16);
    f.write(s_pcapRing[idx], capLen);
    s_pcapBytes += 16 + capLen;
    s_pcapPackets++;
    s_pcapTail = (s_pcapTail + 1) & 3;
  }
  f.close();
}

static void runRogueApScan() {
  s_rogueCount = 0;
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks(false, true);
  s_totalApsLastScan = (n > 0) ? n : 0;

  for (int i = 0; i < n; i++) {
    String ssidA = WiFi.SSID(i);
    if (ssidA.length() == 0) continue;
    uint8_t *bssidA = WiFi.BSSID(i);
    int chA = WiFi.channel(i);
    bool openA = (WiFi.encryptionType(i) == ENC_TYPE_NONE);

    for (int j = i + 1; j < n; j++) {
      String ssidB = WiFi.SSID(j);
      if (ssidA != ssidB) continue;
      uint8_t *bssidB = WiFi.BSSID(j);
      if (!bssidA || !bssidB || memcmp(bssidA, bssidB, 6) == 0) continue;

      int chB = WiFi.channel(j);
      bool openB = (WiFi.encryptionType(j) == ENC_TYPE_NONE);
      bool encDiff = (openA != openB);

      if (encDiff || chA != chB || memcmp(bssidA, bssidB, 3) != 0) {
        if (s_rogueCount < kMaxRogues) {
          RogueAlert &r = s_rogues[s_rogueCount++];
          strncpy(r.ssid, ssidA.c_str(), 20);
          r.ssid[20] = '\0';
          memcpy(r.bssid1, bssidA, 6);
          memcpy(r.bssid2, bssidB, 6);
          r.ch1 = (uint8_t)chA;
          r.ch2 = (uint8_t)chB;
          r.encMismatch = encDiff;
        }
      }
    }
  }
  WiFi.scanDelete();
}

static void runChannelHeatmapScan() {
  for (int ch = 1; ch <= 13; ch++) {
    s_chApCount[ch] = 0;
    s_chBestRssi[ch] = -100;
  }

  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks(false, true);
  for (int i = 0; i < n; i++) {
    int ch = WiFi.channel(i);
    int rssi = WiFi.RSSI(i);
    if (ch >= 1 && ch <= 13) {
      if (s_chApCount[ch] < 50) s_chApCount[ch]++;
      if (rssi > s_chBestRssi[ch]) s_chBestRssi[ch] = (int8_t)rssi;
    }
  }
  WiFi.scanDelete();

  int minScore = 9999;
  int maxScore = -1;
  s_bestFreeCh = 1;
  s_busiestCh = 6;

  for (int ch = 1; ch <= 13; ch++) {
    int rssiWeight = (s_chBestRssi[ch] > -100) ? (105 + s_chBestRssi[ch]) / 3 : 0;
    int score = s_chApCount[ch] * 8 + rssiWeight;
    if (score > 36) score = 36;
    if (score > s_chPeakScore[ch]) s_chPeakScore[ch] = (uint8_t)score;
    else if (s_chPeakScore[ch] > 1) s_chPeakScore[ch]--;

    if (score < minScore && (ch == 1 || ch == 6 || ch == 11)) {
      minScore = score;
      s_bestFreeCh = ch;
    }
    if (score > maxScore) {
      maxScore = score;
      s_busiestCh = ch;
    }
  }
}

static void runWardriveStep() {
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks(false, true);
  s_wardriveScans++;

  if (n > 0 && SPIFFS.begin()) {
    rotateLogIfNeeded("/wardrive.csv", 16384);
    bool exists = SPIFFS.exists("/wardrive.csv");
    File f = SPIFFS.open("/wardrive.csv", "a");
    if (f) {
      if (!exists) {
        f.println("timestamp,ssid,bssid,ch,rssi,enc,kismet_type");
      }
      time_t now = time(nullptr);
      for (int i = 0; i < n; i++) {
        uint8_t *bssid = WiFi.BSSID(i);
        if (!bssid) continue;
        bool alreadySeen = false;
        for (int j = 0; j < s_seenBssidCount; j++) {
          if (memcmp(s_seenBssids[j], bssid, 6) == 0) {
            alreadySeen = true;
            break;
          }
        }
        if (!alreadySeen) {
          if (s_seenBssidCount < 24) {
            memcpy(s_seenBssids[s_seenBssidCount++], bssid, 6);
          } else {
            memmove(&s_seenBssids[0], &s_seenBssids[1], 23 * 6);
            memcpy(s_seenBssids[23], bssid, 6);
          }
          String ssid = WiFi.SSID(i);
          ssid.replace(",", " ");
          if (ssid.length() == 0) ssid = "<HIDDEN>";
          const char *enc = (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? "OPEN" : "WPA2";
          unsigned long ts = (now > 1700000000) ? (unsigned long)now : (unsigned long)(millis() / 1000);
          f.printf("%lu,%s,%s,%d,%d,%s,infrastructure\n",
                   ts,
                   ssid.c_str(),
                   WiFi.BSSIDstr(i).c_str(),
                   WiFi.channel(i),
                   WiFi.RSSI(i),
                   enc);
          s_wardriveTotalLogged++;
        }
      }
      f.close();
    }
  }
  WiFi.scanDelete();
}

void wifiToolsSetup() {
  s_subMode = 0;
  s_menuIdx = 0;
  for (int i = 0; i < 14; i++) s_chPeakScore[i] = 0;
  encClear();
}

void wifiToolsLoop() {
  if (s_subMode == 0) {
    const int kMenuItems = 8;
    int d = encStep();
    if (d != 0) {
      s_menuIdx = (s_menuIdx + d) % kMenuItems;
      if (s_menuIdx < 0) s_menuIdx += kMenuItems;
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawRBox(0, 0, 128, 12, 2);
    u8g2.setDrawColor(0);
    u8g2.drawStr(6, 9, "WIFI DEFENSA & AUDIT");
    u8g2.setDrawColor(1);

    const char *items[kMenuItems] = {
        "1. PROBE + MAC RND",
        "2. CHANNEL HEATMAP",
        "3. WARDRIVE + KISMET",
        "4. ROGUE AP / EVIL TWIN",
        "5. HIDDEN SSID UNMASK",
        "6. KARMA / PINEAPPLE",
        "7. PCAP CAPTURE (.pcap)",
        "8. <- VOLVER AL MENU"};

    int firstVis = s_menuIdx - 2;
    if (firstVis > kMenuItems - 4) firstVis = kMenuItems - 4;
    if (firstVis < 0) firstVis = 0;

    u8g2.setFont(u8g2_font_5x7_tr);
    for (int row = 0; row < 4; row++) {
      int idx = firstVis + row;
      int y = 24 + row * 11;
      if (idx == s_menuIdx) {
        u8g2.drawRBox(3, y - 8, 122, 10, 2);
        u8g2.setDrawColor(0);
        u8g2.drawStr(7, y, items[idx]);
        u8g2.setDrawColor(1);
      } else {
        u8g2.drawStr(7, y, items[idx]);
      }
    }
    u8g2.sendBuffer();

    if (encOk()) {
      if (s_menuIdx == 0) startSnifferMode(1);
      else if (s_menuIdx == 1) { s_subMode = 2; s_lastHeatmapScanMs = 0; }
      else if (s_menuIdx == 2) { s_subMode = 3; s_lastWardriveScanMs = 0; }
      else if (s_menuIdx == 3) { s_subMode = 4; s_lastRogueScanMs = 0; s_rogueScroll = 0; }
      else if (s_menuIdx == 4) startSnifferMode(5);
      else if (s_menuIdx == 5) startSnifferMode(6);
      else if (s_menuIdx == 6) startSnifferMode(7);
      else runningApp = false;
      encClear();
    }
    if (encBack()) {
      runningApp = false;
      encClear();
    }
    return;
  }

  // Submodo 1: Probe Sniffer + MAC Randomizer Detector
  if (s_subMode == 1) {
    if (millis() - s_lastHopMs > 280) {
      s_currentCh = (s_currentCh % 13) + 1;
      wifi_set_channel(s_currentCh);
      s_lastHopMs = millis();
    }

    int d = encStep();
    int total = s_probeCount;
    if (d != 0 && total > 0) {
      s_probeScroll = constrain(s_probeScroll + d, 0, max(0, total - 1));
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(2, 8);
    u8g2.printf("PROBES | RND:%lu REAL:%lu", (unsigned long)s_rndMacCount, (unsigned long)s_realMacCount);
    u8g2.drawHLine(0, 10, 128);

    if (total == 0) {
      u8g2.drawStr(8, 30, "Analizando MACs 802.11");
      u8g2.drawStr(8, 42, "Detectando MAC Random...");
    } else {
      int idx = total - 1 - s_probeScroll;
      if (idx < 0) idx = 0;
      ProbeEntry e = s_probes[idx];
      char macStr[18];
      snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
               e.mac[0], e.mac[1], e.mac[2], e.mac[3], e.mac[4], e.mac[5]);
      u8g2.setCursor(2, 22);
      u8g2.printf("%s %s", macStr, e.isRandomMac ? "[RND]" : "[REAL]");
      u8g2.setCursor(2, 34);
      u8g2.printf("SSID: %s", e.ssid);
      u8g2.setCursor(2, 46);
      u8g2.printf("CH:%d | %ddBm | #%d/%d", e.ch, e.rssi, idx + 1, total);
    }

    u8g2.drawHLine(0, 53, 128);
    u8g2.drawStr(4, 62, "Gira:Clientes  HOLD:Salir");
    u8g2.sendBuffer();

    if (encBack()) {
      stopSnifferMode();
      s_subMode = 0;
      encClear();
    }
    return;
  }

  // Submodo 2: Channel Heatmap
  if (s_subMode == 2) {
    if (s_lastHeatmapScanMs == 0 || (millis() - s_lastHeatmapScanMs > 2500) || encOk()) {
      runChannelHeatmapScan();
      s_lastHeatmapScanMs = millis();
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(2, 8);
    u8g2.printf("HEATMAP | LIBRE:CH%d SAT:CH%d", s_bestFreeCh, s_busiestCh);
    u8g2.drawHLine(0, 10, 128);

    for (int ch = 1; ch <= 13; ch++) {
      int x = 3 + (ch - 1) * 9;
      int h = s_chApCount[ch] * 7 + ((s_chBestRssi[ch] > -100) ? 4 : 0);
      if (h > 36) h = 36;
      if (h > 0) u8g2.drawBox(x, 49 - h, 6, h);
      else u8g2.drawPixel(x + 2, 48);
      int pk = s_chPeakScore[ch];
      if (pk > 36) pk = 36;
      u8g2.drawHLine(x, 49 - pk, 6);
      if (ch % 2 == 1) {
        u8g2.setCursor(x, 57);
        u8g2.print(ch);
      }
    }
    u8g2.drawHLine(0, 49, 128);
    u8g2.sendBuffer();

    if (encBack()) {
      s_subMode = 0;
      encClear();
    }
    return;
  }

  // Submodo 3: Wardriving + Kismet CSV
  if (s_subMode == 3) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawRBox(0, 0, 128, 12, 2);
    u8g2.setDrawColor(0);
    u8g2.drawStr(8, 9, "WARDRIVE + KISMET CSV");
    u8g2.setDrawColor(1);

    time_t now = time(nullptr);
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(4, 24);
    u8g2.printf("Barridos: %lu | APs: %lu", (unsigned long)s_wardriveScans, (unsigned long)s_wardriveTotalLogged);
    u8g2.setCursor(4, 35);
    u8g2.printf("Reloj: %s", (now > 1700000000) ? "NTP REAL SYNC" : "UPTIME LOCAL");
    u8g2.setCursor(4, 46);
    u8g2.print("Export: /wardrive.csv");
    u8g2.drawHLine(0, 52, 128);
    u8g2.drawStr(6, 61, "Descarga en WEB DASHBOARD");
    u8g2.sendBuffer();

    if (s_lastWardriveScanMs == 0 || (millis() - s_lastWardriveScanMs > 3000)) {
      runWardriveStep();
      s_lastWardriveScanMs = millis();
    }

    if (encBack()) {
      s_subMode = 0;
      encClear();
    }
    return;
  }

  // Submodo 4: Rogue AP / Evil Twin Detector
  if (s_subMode == 4) {
    if (s_lastRogueScanMs == 0 || (millis() - s_lastRogueScanMs > 4500) || encOk()) {
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_6x10_tr);
      u8g2.drawStr(8, 11, "ROGUE AP / EVIL TWIN");
      u8g2.drawHLine(0, 13, 128);
      u8g2.setFont(u8g2_font_5x7_tr);
      u8g2.drawStr(10, 34, "Auditando BSSIDs y");
      u8g2.drawStr(10, 44, "clones de SSID...");
      u8g2.sendBuffer();
      runRogueApScan();
      s_lastRogueScanMs = millis();
    }

    int d = encStep();
    if (d != 0 && s_rogueCount > 0) {
      s_rogueScroll = constrain(s_rogueScroll + d, 0, s_rogueCount - 1);
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(2, 8);
    u8g2.printf("EVIL TWIN | APs:%d ALERT:%d", s_totalApsLastScan, s_rogueCount);
    u8g2.drawHLine(0, 10, 128);

    if (s_rogueCount == 0) {
      u8g2.drawStr(8, 28, "ESTADO: RED LIMPIA (OK)");
      u8g2.drawStr(8, 40, "Sin clones ni downgrade");
    } else {
      RogueAlert &r = s_rogues[s_rogueScroll];
      u8g2.setCursor(2, 22);
      u8g2.printf("! CLON: %s", r.ssid);
      u8g2.setCursor(2, 33);
      u8g2.printf("B1:%02X:%02X:%02X CH:%d", r.bssid1[3], r.bssid1[4], r.bssid1[5], r.ch1);
      u8g2.setCursor(2, 44);
      u8g2.printf("B2:%02X:%02X:%02X CH:%d %s", r.bssid2[3], r.bssid2[4], r.bssid2[5], r.ch2,
                  r.encMismatch ? "OPEN!" : "DUP");
    }
    u8g2.drawHLine(0, 52, 128);
    u8g2.drawStr(4, 61, "OK:Re-Scan  HOLD:Volver");
    u8g2.sendBuffer();

    if (encBack()) {
      s_subMode = 0;
      encClear();
    }
    return;
  }

  // Submodo 5: Hidden SSID Unmasker
  if (s_subMode == 5) {
    if (millis() - s_lastHopMs > 260) {
      s_currentCh = (s_currentCh % 13) + 1;
      wifi_set_channel(s_currentCh);
      s_lastHopMs = millis();
    }

    int d = encStep();
    int total = s_hiddenCount;
    if (d != 0 && total > 0) {
      s_probeScroll = constrain(s_probeScroll + d, 0, total - 1);
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(2, 8);
    u8g2.printf("HIDDEN SSID | REV:%d/%d CH%d", s_unmaskedTotal, total, s_currentCh);
    u8g2.drawHLine(0, 10, 128);

    if (total == 0) {
      u8g2.drawStr(6, 28, "Buscando redes ocultas");
      u8g2.drawStr(6, 40, "y respuestas ProbeResp...");
    } else {
      HiddenNetEntry e = s_hidden[s_probeScroll];
      u8g2.setCursor(2, 22);
      u8g2.printf("BSSID: %02X:%02X:%02X:%02X:%02X:%02X",
                  e.bssid[0], e.bssid[1], e.bssid[2], e.bssid[3], e.bssid[4], e.bssid[5]);
      u8g2.setCursor(2, 34);
      u8g2.printf("SSID: %s", e.ssid);
      u8g2.setCursor(2, 46);
      u8g2.printf("CH:%d | %ddBm | %s", e.ch, e.rssi, e.unmasked ? "REVELADO!" : "ESPERANDO");
    }
    u8g2.drawHLine(0, 52, 128);
    u8g2.drawStr(4, 61, "Gira:Ver  HOLD:Volver");
    u8g2.sendBuffer();

    if (encBack()) {
      stopSnifferMode();
      s_subMode = 0;
      encClear();
    }
    return;
  }

  // Submodo 6: Karma / WiFi Pineapple Detector
  if (s_subMode == 6) {
    if (millis() - s_lastHopMs > 260) {
      s_currentCh = (s_currentCh % 13) + 1;
      wifi_set_channel(s_currentCh);
      s_lastHopMs = millis();
    }

    int d = encStep();
    int total = s_karmaCount;
    if (d != 0 && total > 0) {
      s_probeScroll = constrain(s_probeScroll + d, 0, total - 1);
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(2, 8);
    u8g2.printf("KARMA DETECT | ALERTA:%d", s_karmaSuspects);
    u8g2.drawHLine(0, 10, 128);

    if (total == 0) {
      u8g2.drawStr(6, 28, "Monitorizando ProbeResp");
      u8g2.drawStr(6, 40, "Anti-WiFi Pineapple...");
    } else {
      KarmaEntry k = s_karma[s_probeScroll];
      u8g2.setCursor(2, 22);
      u8g2.printf("MAC:%02X:%02X:%02X:%02X (%ddBm)",
                  k.bssid[2], k.bssid[3], k.bssid[4], k.bssid[5], k.rssi);
      u8g2.setCursor(2, 34);
      u8g2.printf("SSIDs distintos: %d %s", k.distinctCount,
                  (k.distinctCount >= 3) ? "[KARMA!]" : "[NORMAL]");
      u8g2.setCursor(2, 46);
      u8g2.printf("Ult: %s", k.lastSsids[0]);
    }
    u8g2.drawHLine(0, 52, 128);
    u8g2.drawStr(4, 61, "Gira:APs  HOLD:Volver");
    u8g2.sendBuffer();

    if (encBack()) {
      stopSnifferMode();
      s_subMode = 0;
      encClear();
    }
    return;
  }

  // Submodo 7: PCAP Capture (.pcap)
  if (s_subMode == 7) {
    flushPcapRingToSpiffs();
    if (millis() - s_lastHopMs > 400) {
      s_currentCh = (s_currentCh % 13) + 1;
      wifi_set_channel(s_currentCh);
      s_lastHopMs = millis();
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawRBox(0, 0, 128, 12, 2);
    u8g2.setDrawColor(0);
    u8g2.drawStr(8, 9, "PCAP SNIFFER (.pcap)");
    u8g2.setDrawColor(1);

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(4, 24);
    u8g2.printf("Canal Activo: CH %02d", s_currentCh);
    u8g2.setCursor(4, 35);
    u8g2.printf("Paquetes 802.11: %lu", (unsigned long)s_pcapPackets);
    u8g2.setCursor(4, 46);
    u8g2.printf("Archivo: %lu bytes", (unsigned long)s_pcapBytes);
    u8g2.drawHLine(0, 52, 128);
    u8g2.drawStr(4, 61, "Web: /download-pcap | HOLD");
    u8g2.sendBuffer();

    if (encBack()) {
      stopSnifferMode();
      s_subMode = 0;
      encClear();
    }
  }
}
