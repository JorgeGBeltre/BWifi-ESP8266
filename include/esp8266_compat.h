#ifndef ESP8266_COMPAT_H
#define ESP8266_COMPAT_H

#include <Arduino.h>
#include <EEPROM.h>
#include <LittleFS.h>
#include <RF24.h>
#ifdef printf_P
#undef printf_P
#endif

// Alias SPIFFS -> LittleFS para mantener compatibilidad de rutas (/logins.txt, /wardrive.csv, /trackers.txt)
#ifndef SPIFFS
#define SPIFFS LittleFS
#endif

// Modos de apertura compatibles con ESP32 FS
#ifndef FILE_WRITE
#define FILE_WRITE "w"
#endif
#ifndef FILE_APPEND
#define FILE_APPEND "a"
#endif
#ifndef FILE_READ
#define FILE_READ "r"
#endif

// Indicador global de si el segundo módulo nRF24L01+ (jam2) está conectado físicamente
extern bool g_dualRadioAvailable;

// Helper de síntesis BLE ADV_NONCONN_IND sobre nRF24L01+ (Canales BLE 37=2, 38=26, 39=80)
namespace BleRf24 {

inline uint8_t swapBits(uint8_t a) {
  uint8_t v = 0;
  if (a & 0x80) v |= 0x01;
  if (a & 0x40) v |= 0x02;
  if (a & 0x20) v |= 0x04;
  if (a & 0x10) v |= 0x08;
  if (a & 0x08) v |= 0x10;
  if (a & 0x04) v |= 0x20;
  if (a & 0x02) v |= 0x40;
  if (a & 0x01) v |= 0x80;
  return v;
}

inline void bleCrc(const uint8_t *data, uint8_t len, uint8_t *dst) {
  dst[0] = 0xAA;
  dst[1] = 0xAA;
  dst[2] = 0xAA;
  while (len--) {
    uint8_t d = *(data++);
    for (uint8_t i = 1; i; i <<= 1, d >>= 1) {
      uint8_t t = dst[0] & 0x01;
      dst[0] >>= 1;
      if (dst[1] & 0x01) dst[0] |= 0x80;
      dst[1] >>= 1;
      if (dst[2] & 0x01) dst[1] |= 0x80;
      dst[2] >>= 1;
      if (t != (d & 1)) {
        dst[2] ^= 0xDA;
        dst[1] ^= 0x60;
      }
    }
  }
}

inline void bleWhiten(uint8_t *data, uint8_t len, uint8_t whitenCoeff) {
  while (len--) {
    uint8_t m = 1;
    for (uint8_t i = 0; i < 8; i++, m <<= 1) {
      if (whitenCoeff & 0x80) {
        whitenCoeff ^= 0x11;
        (*data) ^= m;
      }
      whitenCoeff <<= 1;
    }
    data++;
  }
}

inline void configureRadioForBle(RF24 &radio) {
  const uint8_t bleAccessAddress[4] = {0x71, 0x91, 0x7D, 0x6B}; // 0x8E89BED6 bit-swapped
  radio.stopListening();
  radio.setAutoAck(false);
  radio.setPALevel(RF24_PA_MAX);
  radio.setDataRate(RF24_1MBPS);
  radio.disableCRC();
  radio.setAddressWidth(4);
  radio.setRetries(0, 0);
  radio.openWritingPipe(bleAccessAddress);
  radio.openReadingPipe(1, bleAccessAddress);
}

// Transmite un paquete BLE ADV_NONCONN_IND (hasta 32 bytes de ventana nRF24) en los canales 37, 38 y 39
inline void transmitAdv(RF24 &radio, const uint8_t *advPayload, uint8_t payloadLen) {
  static const uint8_t rfChannels[3] = {2, 26, 80};
  static const uint8_t bleChIdx[3] = {37, 38, 39};

  if (payloadLen > 21) payloadLen = 21; // 2 header + 6 MAC + 21 payload + 3 CRC = 32 bytes max nRF24
  uint8_t pkt[32];
  for (int c = 0; c < 3; c++) {
    uint8_t pls = 0;
    pkt[pls++] = 0x42; // ADV_NONCONN_IND + TxAdd=random
    pkt[pls++] = payloadLen + 6;
    // Random MAC
    pkt[pls++] = (uint8_t)random(0x10, 0xFE);
    pkt[pls++] = (uint8_t)random(0x10, 0xFE);
    pkt[pls++] = (uint8_t)random(0x10, 0xFE);
    pkt[pls++] = (uint8_t)random(0x10, 0xFE);
    pkt[pls++] = (uint8_t)random(0x10, 0xFE);
    pkt[pls++] = (uint8_t)(random(0xC0, 0xFE) | 0xC0);

    memcpy(&pkt[pls], advPayload, payloadLen);
    pls += payloadLen;

    bleCrc(pkt, pls, &pkt[pls]);
    pls += 3;

    uint8_t coeff = swapBits(bleChIdx[c]) | 2;
    bleWhiten(pkt, pls, coeff);
    for (uint8_t i = 0; i < pls; i++) {
      pkt[i] = swapBits(pkt[i]);
    }

    radio.setChannel(rfChannels[c]);
    radio.write(pkt, pls);
  }
}

} // namespace BleRf24

#endif

