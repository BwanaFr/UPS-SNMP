#include <Arduino.h>

#include <ETH.h>
#include <SPI.h>
#include <WiFi.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>

#include "UI.hpp"
#include "NavButton.hpp"

#include "DevicePins.hpp"

#include "esp_log.h"



static const char* TAG = "Main";


void WiFiEvent(arduino_event_id_t event)
{
    switch (event) {
    case ARDUINO_EVENT_ETH_START:
        break;
    case ARDUINO_EVENT_ETH_CONNECTED:
        ESP_LOGI(TAG, "ETH Connected");
        break;
    case ARDUINO_EVENT_ETH_GOT_IP:
        ESP_LOGI(TAG, "ETH MAC: %s, IPv4: %s, Duplex: %s, %uMbps", ETH.macAddress().c_str(), ETH.localIP().toString().c_str(),
                    ETH.fullDuplex() ? "FULL" : "HALF", ETH.linkSpeed());
        ui.gotIP(ETH.localIP().toString().c_str());
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
        ESP_LOGI(TAG, "ETH Disconnected, Stop services");
        ui.gotIP("");
        break;
    case ARDUINO_EVENT_ETH_STOP:
        ESP_LOGI(TAG, "ETH Stopped");
        break;
    default:
        break;
    }
}

void setup()
{

    //Starts serial
    Serial.begin(115200);

    //Starts user interraction
    ui.begin();

    //Starts Ethernet
    WiFi.onEvent(WiFiEvent);
    ESP_LOGI(TAG, "Starting ethernet");
    if (!ETH.begin(ETH_PHY_W5500, 1, ETH_CS_PIN, ETH_INT_PIN, ETH_RST_PIN,
                    SPI3_HOST,
                    ETH_SCLK_PIN, ETH_MISO_PIN, ETH_MOSI_PIN, 60 /* SPI clock speed MHz*/)) {
        ESP_LOGE(TAG, "Ethernet start Failed!");
        UserLed::getInstance()->fatal();
    }
}

void loop()
{
    //Don't need this Arduino shit
    vTaskDelete(NULL);
}
