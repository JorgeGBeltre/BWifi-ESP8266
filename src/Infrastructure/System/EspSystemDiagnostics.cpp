#include "Infrastructure/System/EspSystemDiagnostics.h"
#include "Infrastructure/Input/encoder_input.h"
#include <Arduino.h>
#include <LittleFS.h>

namespace bwifi::infra {

EspSystemDiagnostics::EspSystemDiagnostics(RF24& radio1, RF24& radio2, uint32_t& bootCount)
    : radio1_(radio1), radio2_(radio2), bootCount_(bootCount) {}

void EspSystemDiagnostics::incrementBootCount() {
    bootCount_++;
}

domain::HardwareDiagnostics EspSystemDiagnostics::collectDiagnostics() const {
    domain::HardwareDiagnostics diag;
    diag.nrf1Connected        = radio1_.isChipConnected();
    diag.nrf2Connected        = radio2_.isChipConnected();
    diag.spiffsMounted        = LittleFS.begin();
    if (diag.spiffsMounted) {
        FSInfo fsInfo;
        if (LittleFS.info(fsInfo)) {
            diag.spiffsUsedKb  = fsInfo.usedBytes / 1024;
            diag.spiffsTotalKb = fsInfo.totalBytes / 1024;
        }
    }
    diag.freeHeapKb           = ESP.getFreeHeap() / 1024;
    diag.bootCount            = bootCount_;
    diag.encoderButtonPressed = ::encSwPressed();
    return diag;
}

} // namespace bwifi::infra
