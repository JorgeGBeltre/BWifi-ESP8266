#pragma once

#include <cstdint>

namespace bwifi::infra {

struct BoardConfig {
    static constexpr const char* kFirmwareVersion = "3.5";

    // I2C SSD1306 128x64 OLED Display (NodeMCU: D6=12 SCL, D5=14 SDA)
    static constexpr uint8_t  kOledSclPin     = 12;
    static constexpr uint8_t  kOledSdaPin     = 14;
    static constexpr uint32_t kOledBusClockHz = 400000;

    // Dual nRF24L01+ PA+LNA SPI Radios (ESP8266 HSPI: SCK=14, MISO=12, MOSI=13)
    static constexpr uint8_t  kNrf1CePin      = 4;   // D2
    static constexpr uint8_t  kNrf1CsnPin     = 15;  // D8
    static constexpr uint8_t  kNrf2CePin      = 2;   // D4
    static constexpr uint8_t  kNrf2CsnPin     = 1;   // TX (GPIO 1)
    static constexpr uint32_t kNrfSpiSpeedHz  = 10000000;

    // HW-040 (KY-040) Quadrature Rotary Encoder (NodeMCU: D1=5 CLK, D3=0 DT, D0=16 SW)
    static constexpr uint8_t  kEncClkPin      = 5;
    static constexpr uint8_t  kEncDtPin       = 0;
    static constexpr uint8_t  kEncSwPin       = 16;

    // Web Dashboard HTTP Port
    static constexpr uint16_t kWebServerPort  = 80;
};

} // namespace bwifi::infra
