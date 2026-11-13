#include <Arduino.h>
#include <U8g2lib.h>
#include <RF24.h>
#include "encoder_input.h"
#include "esp8266_compat.h"

extern U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2;
extern RF24 jam1;
extern RF24 jam2;
extern bool runningApp;

#define CHANNELS 125
uint8_t values[CHANNELS];
uint8_t peak_heights[CHANNELS];

#define WF_ROWS 28
#define WF_BYTES_PER_ROW 16
static uint8_t s_waterfall[WF_ROWS][WF_BYTES_PER_ROW];
// 0: Espectro Barras, 1: Waterfall 2D, 2: RF Fingerprint IA, 3: MouseJack Sniffer + Replay
static int s_viewMode = 0;

static uint8_t s_esbLastFrame[16];
static uint8_t s_esbLastCh = 5;
static uint32_t s_esbHitCount = 0;
static uint32_t s_esbReplayCount = 0;
static uint8_t s_mjScanCh = 2;

const int UMBRAL_ENERGIA_TOTAL = 800;
const int REPETICIONES_LECTURA = 35;

static void configureEsbPromisc() {
    jam1.stopListening();
    jam1.setAutoAck(false);
    jam1.setDataRate(RF24_2MBPS);
    jam1.disableCRC();
    jam1.setAddressWidth(3);
    const uint8_t promiscAddr[3] = {0xAA, 0x00, 0x00};
    jam1.openReadingPipe(1, promiscAddr);
    jam1.setPayloadSize(16);
    jam1.startListening();
}

void spectrographSetup() {
    jam1.begin();
    jam1.setAutoAck(false);
    jam1.setDataRate(RF24_1MBPS);
    jam1.disableCRC();
    jam1.startListening();

    if (g_dualRadioAvailable) {
        jam2.begin();
        jam2.setAutoAck(false);
        jam2.setDataRate(RF24_1MBPS);
        jam2.disableCRC();
        jam2.startListening();
    }

    s_viewMode = 0;
    s_esbHitCount = 0;
    s_esbReplayCount = 0;
    memset(values, 0, sizeof(values));
    memset(peak_heights, 0, sizeof(peak_heights));
    memset(s_waterfall, 0, sizeof(s_waterfall));
    memset(s_esbLastFrame, 0, sizeof(s_esbLastFrame));
}

static void runMouseJackMode() {
    for (int step = 0; step < 8; step++) {
        s_mjScanCh = (s_mjScanCh + 3) % 84;
        if (s_mjScanCh < 2) s_mjScanCh = 2;
        jam1.setChannel(s_mjScanCh);
        jam1.startListening();
        delayMicroseconds(380);
        if (jam1.available()) {
            uint8_t raw[16];
            jam1.read(raw, 16);
            int transitions = 0;
            for (int i = 0; i < 15; i++) {
                if (raw[i] != raw[i + 1] && raw[i] != 0x00 && raw[i] != 0xFF) transitions++;
            }
            if (transitions >= 5) {
                memcpy(s_esbLastFrame, raw, 16);
                s_esbLastCh = s_mjScanCh;
                s_esbHitCount++;
            }
        }
    }

    if (encAux() && s_esbHitCount > 0) {
        jam1.stopListening();
        const uint8_t txAddr[3] = {s_esbLastFrame[0], s_esbLastFrame[1], s_esbLastFrame[2]};
        jam1.openWritingPipe(txAddr);
        jam1.setChannel(s_esbLastCh);
        for (int r = 0; r < 5; r++) {
            jam1.write(&s_esbLastFrame[3], 13, true);
            delayMicroseconds(500);
        }
        s_esbReplayCount++;
        configureEsbPromisc();
    }

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.drawRBox(0, 0, 128, 12, 2);
    u8g2.setDrawColor(0);
    u8g2.drawStr(6, 9, "MOUSEJACK / ESB LAB");
    u8g2.setDrawColor(1);

    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setCursor(2, 22);
    u8g2.printf("Scan:2.%03dG | Hits:%lu", 400 + s_mjScanCh, (unsigned long)s_esbHitCount);

    if (s_esbHitCount == 0) {
        u8g2.drawStr(4, 34, "Buscando tramas ESB 2Mbps");
        u8g2.drawStr(4, 45, "(Ratones/Teclados 2.4G)");
    } else {
        u8g2.setCursor(2, 33);
        u8g2.printf("ADDR: %02X:%02X:%02X:%02X (CH %d)",
                    s_esbLastFrame[0], s_esbLastFrame[1], s_esbLastFrame[2], s_esbLastFrame[3], s_esbLastCh);
        u8g2.setCursor(2, 43);
        u8g2.printf("PLD: %02X %02X %02X %02X | R:%lu",
                    s_esbLastFrame[4], s_esbLastFrame[5], s_esbLastFrame[6], s_esbLastFrame[7],
                    (unsigned long)s_esbReplayCount);
        u8g2.drawStr(2, 52, "SW+Giro: REPLAY Trama ESB");
    }

    u8g2.drawHLine(0, 54, 128);
    u8g2.drawStr(2, 62, "[OK]Modo  [HOLD]Salir");
    u8g2.sendBuffer();
}

void spectrographLoop() {
    if (encBack()) {
        jam1.stopListening();
        if (g_dualRadioAvailable) jam2.stopListening();
        runningApp = false;
        encClear();
        return;
    }

    if (encOk()) {
        s_viewMode = (s_viewMode + 1) % 4;
        if (s_viewMode == 3) {
            configureEsbPromisc();
        } else {
            jam1.setDataRate(RF24_1MBPS);
            jam1.startListening();
            if (g_dualRadioAvailable) {
                jam2.setDataRate(RF24_1MBPS);
                jam2.startListening();
            }
        }
        encClear();
    }

    if (s_viewMode == 3) {
        runMouseJackMode();
        return;
    }

    long total_energy = 0;

    for (int i = 0; i < 62; i++) {
        if ((i & 7) == 0) {
            encoderUpdate();
            if (encBack()) {
                jam1.stopListening();
                if (g_dualRadioAvailable) jam2.stopListening();
                runningApp = false;
                return;
            }
            if (encOk()) {
                s_viewMode = (s_viewMode + 1) % 4;
                if (s_viewMode == 3) {
                    configureEsbPromisc();
                    return;
                }
            }
        }
        int c1 = 0, c2 = 0;
        for (int j = 0; j < REPETICIONES_LECTURA; j++) {
            jam1.setChannel(i);
            if (jam1.testRPD() || jam1.testCarrier()) c1++;

            if (g_dualRadioAvailable) {
                jam2.setChannel(i + 63);
                if (jam2.testRPD() || jam2.testCarrier()) c2++;
            } else {
                jam1.setChannel(i + 63);
                if (jam1.testRPD() || jam1.testCarrier()) c2++;
            }
        }
        values[i] = c1;
        values[i + 63] = c2;
        total_energy += (c1 + c2);
    }

    for (int r = WF_ROWS - 1; r > 0; r--) {
        memcpy(s_waterfall[r], s_waterfall[r - 1], WF_BYTES_PER_ROW);
    }
    memset(s_waterfall[0], 0, WF_BYTES_PER_ROW);
    for (int x = 0; x < CHANNELS; x++) {
        if (values[x] >= 3) {
            s_waterfall[0][x >> 3] |= (uint8_t)(1U << (x & 7));
        }
    }

    u8g2.clearBuffer();

    if (s_viewMode == 2) {
        int activeChannels = 0;
        int contiguousMax = 0;
        int curContig = 0;
        int bleAdvEnergy = values[2] + values[26] + values[80];
        int peakCh = 0;
        int peakVal = 0;

        for (int x = 0; x < CHANNELS; x++) {
            if (values[x] > peakVal) { peakVal = values[x]; peakCh = x; }
            if (values[x] >= 3) {
                activeChannels++;
                curContig++;
                if (curContig > contiguousMax) contiguousMax = curContig;
            } else {
                curContig = 0;
            }
        }

        const char *proto = "RUIDO FONDO / IDLE";
        if (contiguousMax >= 12) proto = "WIFI 802.11 (20MHz)";
        else if (activeChannels >= 14 && contiguousMax <= 5) proto = "BLE / BT FHSS HOP";
        else if (bleAdvEnergy >= 6) proto = "BLE BEACON / ADV";
        else if (contiguousMax >= 2 && contiguousMax <= 5 && peakVal >= 5) proto = "ZIGBEE / NRF ESB";

        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawRBox(0, 0, 128, 12, 2);
        u8g2.setDrawColor(0);
        u8g2.drawStr(6, 9, "RF FINGERPRINT IA");
        u8g2.setDrawColor(1);

        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.setCursor(2, 22);
        u8g2.printf("FIRMA: %s", proto);
        u8g2.setCursor(2, 33);
        u8g2.printf("Pico: 2.%03d GHz (CH %d)", 400 + peakCh, peakCh);
        u8g2.setCursor(2, 44);
        u8g2.printf("BW:%dMHz | Act:%d | E:%ld", contiguousMax, activeChannels, total_energy);
        u8g2.drawHLine(0, 52, 128);
        u8g2.drawStr(2, 61, "[OK] MouseJack  [HOLD]Salir");
        u8g2.sendBuffer();
        return;
    }

    if (total_energy > UMBRAL_ENERGIA_TOTAL) {
        if ((millis() / 150) % 2 == 0) {
            u8g2.drawBox(0, 0, 128, 12);
            u8g2.setDrawColor(0);
            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.drawStr(15, 9, "! ATAQUE DETECTADO !");
            u8g2.setDrawColor(1);
        }
    } else {
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(2, 7, (s_viewMode == 1) ? "2.4GHz WATERFALL" : "2.4GHz ESPECTRO");
        u8g2.setCursor(90, 7);
        u8g2.print("[OK]MODO");
    }
    u8g2.drawHLine(0, 9, 128);

    if (s_viewMode == 0) {
        for (int x = 0; x < CHANNELS; x++) {
            int bar_h = map(values[x], 0, REPETICIONES_LECTURA, 0, 42);
            if (bar_h > 42) bar_h = 42;
            if (bar_h > 0) {
                u8g2.drawVLine(x + 1, 53 - bar_h, bar_h);
            }
            if (bar_h >= peak_heights[x]) {
                peak_heights[x] = bar_h;
            } else {
                if (millis() % 2 == 0 && peak_heights[x] > 0) peak_heights[x]--;
            }
            if (peak_heights[x] > 0) {
                u8g2.drawPixel(x + 1, 53 - peak_heights[x]);
            }
        }
    } else {
        for (int r = 0; r < WF_ROWS; r++) {
            int y = 12 + r;
            for (int x = 0; x < CHANNELS; x++) {
                if (s_waterfall[r][x >> 3] & (1U << (x & 7))) {
                    u8g2.drawPixel(x + 1, y);
                }
            }
        }
        for (int x = 0; x < CHANNELS; x++) {
            int h = map(values[x], 0, REPETICIONES_LECTURA, 0, 10);
            if (h > 10) h = 10;
            if (h > 0) u8g2.drawVLine(x + 1, 53 - h, h);
        }
    }

    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawHLine(0, 54, 128);
    u8g2.drawStr(2, 62, "2.40G");
    u8g2.drawStr(52, 62, "2.45G");
    u8g2.drawStr(104, 62, "2.52G");

    u8g2.sendBuffer();
}
