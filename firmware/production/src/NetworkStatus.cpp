#include <NetworkStatus.hpp>
#include "esp_log.h"
#include <sstream>

static const char* TAG="NetworkStatus";

NetworkStatus::NetworkStatus() :
    StatusProvider("Network"),
    linkStatus_(this, "Status"),
    deviceIP_(this, "IP"),
    subnetMask_(this, "Mask"),
    deviceGateway_(this, "Router"),
    macAddress_(this, "MAC")
{
    macAddress_.setSNMPOID("1.3.6.1.2.1.47.1.1.1.1.11");
}

void NetworkStatus::networkEvent(arduino_event_id_t event)
{
    switch (event) {
    case ARDUINO_EVENT_ETH_CONNECTED:
    case ARDUINO_EVENT_ETH_GOT_IP:
    {
        std::ostringstream linkStatus;
        linkStatus << ETH.linkSpeed();
        linkStatus << "Mbps ";
        if(ETH.fullDuplex()){
            linkStatus << "FULL";
        }else{
            linkStatus << "HALF";
        }
        linkStatus_.setValue(linkStatus.str());
        deviceIP_.setValue(ETH.localIP());
        deviceIP_.setEnabled(true);
        subnetMask_.setValue(ETH.subnetMask());
        subnetMask_.setEnabled(true);
        deviceGateway_.setValue(ETH.gatewayIP());
        deviceGateway_.setEnabled(true);
        macAddress_.setValue(ETH.macAddress().c_str());
    }
        break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
    {
        std::ostringstream linkStatus;
        linkStatus_.setValue("disconnected");
        deviceIP_.setValue(ETH.localIP());
        deviceIP_.setEnabled(false);
        subnetMask_.setValue(ETH.subnetMask());
        subnetMask_.setEnabled(false);
        deviceGateway_.setValue(ETH.gatewayIP());
        deviceGateway_.setEnabled(false);
        macAddress_.setValue(ETH.macAddress().c_str());
    }
        break;
    default:
        break;
    }
}

NetworkStatus networkStatus;
