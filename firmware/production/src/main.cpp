#include <Arduino.h>

#include <ETH.h>
#include <SPI.h>
#include <Webserver.hpp>
#include <WiFi.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>

#include "CiscoSwitchInfo.hpp"
#include "UPSSNMPAgent.hpp"
#include "UPSHIDDevice.hpp"
#include "Configuration.hpp"
#include "UI.hpp"
#include "GlobalStatus.hpp"
#include "NetworkStatus.hpp"
#include "NavButton.hpp"

#ifdef HAS_TEMP_PROBE
#include "Temperature.hpp"
#endif

#include "DevicePins.hpp"

#include "Pinger.hpp"
#include "esp_log.h"


UPSSNMPAgent snmpAgent;


static const char* TAG = "Main";

/**
 * Configuration changed
 */
void configChanged(DeviceConfiguration::Parameter what)
{
    switch(what){
        case DeviceConfiguration::Parameter::DEVICE_NAME:
        {
            std::string deviceName;
            configuration.getDeviceName(deviceName);
            ETH.setHostname(deviceName.c_str());
            break;
        }
        case DeviceConfiguration::Parameter::IP_CONFIGURATION:
        {
            IPAddress ip, subnet, gateway;
            bool useDHCP;
            configuration.getIPConfiguration(useDHCP, ip, subnet, gateway);
            //Configure ETH
            if(useDHCP){
                ESP_LOGI(TAG, "New IP config : Use DHCP");
                ETH.config();
            }else{
                ESP_LOGI(TAG, "New IP config :");
                ESP_LOGI(TAG, "IP: %s", ip.toString().c_str());
                ESP_LOGI(TAG, "Subnet: %s", subnet.toString().c_str());
                ESP_LOGI(TAG, "Gateway: %s", gateway.toString().c_str());
                ETH.config(ip, gateway, subnet);
            }
            break;
        }
        case DeviceConfiguration::Parameter::PING_TEST_CONFIGURATION:
        {
            bool useGateway = false;
            IPAddress ip = INADDR_NONE;
            int interval = 0;
            int timeout = 0;
            configuration.getPingTestConfiguration(useGateway, ip, interval, timeout);
            // ESP_LOGI(TAG, "New ping config : GW? %d, IP:%s, Interval: %d, Timeout: %d", useGateway, ip.toString().c_str(), interval, timeout);
            pinger.configure(useGateway, ip, interval, timeout);
            break;
        }
        case DeviceConfiguration::Parameter::SNMP_COMMUNITY:
        {
            std::string snmpComStr;
            configuration.getSNMPPublicCommunity(snmpComStr);
            snmpAgent.setSNMPCommunity(snmpComStr);
            ciscoFetcher.setSNMPCommunity(snmpComStr);
            break;
        }
        case DeviceConfiguration::Parameter::SWITCH_IP:
        {
            IPAddress ip;
            configuration.getSwitchIP(ip);
            ciscoFetcher.setSwitchIPAddress(ip);
            break;
        }
    }
}

void WiFiEvent(arduino_event_id_t event)
{
    networkStatus.networkEvent(event);
    switch (event) {
    case ARDUINO_EVENT_ETH_START:
    {
        std::string deviceName;
        configuration.getDeviceName(deviceName);
        ETH.setHostname(deviceName.c_str());
    }
        break;
    case ARDUINO_EVENT_ETH_CONNECTED:
        ESP_LOGI(TAG, "ETH Connected");
        break;
    case ARDUINO_EVENT_ETH_GOT_IP:
        ESP_LOGI(TAG, "ETH MAC: %s, IPv4: %s, Duplex: %s, %uMbps", ETH.macAddress().c_str(), ETH.localIP().toString().c_str(),
                    ETH.fullDuplex() ? "FULL" : "HALF", ETH.linkSpeed());
        //Ethernet available starts network services
        webServer.start();
        snmpAgent.start();
        pinger.start();
        ciscoFetcher.start();
        ui.gotIP();
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
        ESP_LOGI(TAG, "ETH Disconnected, Stop services");
        webServer.stop();
        snmpAgent.stop();
        pinger.stop();
        ciscoFetcher.stop();
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
    //Removes logging from ESP32PingMs lib
    esp_log_level_set("ping", ESP_LOG_ERROR);

    //Starts serial
    Serial.begin(115200);

    //Stops ESP if last reset is panic
    // esp_reset_reason_t reason = esp_reset_reason();
    // if(reason == ESP_RST_PANIC){
    //     while(true){
    //         delay(1);
    //     }
    // }

    //Initializes configuration
    configuration.begin();

    //Register a listener to know configuration changes
    configuration.registerListener(configChanged);

#ifdef HAS_TEMP_PROBE
    //Starts temperature readout
    tempProbe.begin();
#endif
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

    //Loads configuration from flash (Default used if flash empty)
    //Loading the configuration will send a IP configuration change event
    configuration.load();

    //Starts SNMP agent
    snmpAgent.begin();

    //Configure HID bridge
    upsDevice.begin();

    //Setup the web server
    webServer.setup();

    //Global device status reading
    globalStatus.begin();

}

void loop()
{
    //Don't need this Arduino shit
    vTaskDelete(NULL);
}
