#pragma once

#include <ESP8266WebServer.h>
#include <RF24.h>
#include "Domain/Interfaces/IRadioStackManager.h"

namespace bwifi::infra {

/**
 * @brief Concrete IRadioStackManager adapter managing dual nRF24L01+ radios,
 * ESP-NOW slave link, Wi-Fi promiscuous/SoftAP state, and ESP8266WebServer lifecycle.
 */
class Esp8266RadioStackManager : public domain::IRadioStackManager {
public:
    Esp8266RadioStackManager(RF24& radio1, RF24& radio2, ESP8266WebServer& webServer);

    void initRadios() override;
    void stopAllRadios() override;
    void stopWebServer() override;

    bool isNrf1Connected() const override;
    bool isNrf2Connected() const override;

private:
    RF24&             radio1_;
    RF24&             radio2_;
    ESP8266WebServer& webServer_;
};

} // namespace bwifi::infra
