#include "Infrastructure/Radio/Esp8266RadioStackManager.h"
#include "Infrastructure/EspNow/slave_control.h"
#include <ESP8266WiFi.h>
extern "C" {
#include "user_interface.h"
}

extern bool g_dualRadioAvailable;

namespace bwifi::infra {

Esp8266RadioStackManager::Esp8266RadioStackManager(RF24& radio1, RF24& radio2, ESP8266WebServer& webServer)
    : radio1_(radio1), radio2_(radio2), webServer_(webServer) {}

void Esp8266RadioStackManager::initRadios() {
    ::slaveControlSetup();
    bool r1 = radio1_.begin();
    bool r2 = radio2_.begin();
    ::g_dualRadioAvailable = (r1 && r2) || (radio1_.isChipConnected() && radio2_.isChipConnected());
}

void Esp8266RadioStackManager::stopAllRadios() {
    wifi_promiscuous_enable(0);
    wifi_set_promiscuous_rx_cb(nullptr);
    if (WiFi.getMode() != WIFI_OFF) {
        WiFi.mode(WIFI_OFF);
    }
    radio1_.stopListening();
    radio2_.stopListening();
    radio1_.stopConstCarrier();
    radio2_.stopConstCarrier();
}

void Esp8266RadioStackManager::stopWebServer() {
    webServer_.stop();
    WiFi.softAPdisconnect(true);
}

bool Esp8266RadioStackManager::isNrf1Connected() const {
    return radio1_.isChipConnected();
}

bool Esp8266RadioStackManager::isNrf2Connected() const {
    return radio2_.isChipConnected();
}

} // namespace bwifi::infra
