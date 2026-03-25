#ifndef _NETWORKSTATUS_HPP__
#define _NETWORKSTATUS_HPP__

#include <StatusProvider.hpp>
#include <Arduino.h>
#include <FreeRTOS.h>
#include <ETH.h>
#include <string>

/**
 * Device network status
 */
class NetworkStatus : public StatusProvider {
public:
    /**
     * Default constructor
     */
    NetworkStatus();

    /**
     * Default destructor
     */
    virtual ~NetworkStatus() = default;

    /**
     * Called on network event
     */
    void networkEvent(arduino_event_id_t event);

private:
    StatusValue<std::string> linkStatus_;
    StatusValue<IPAddress> deviceIP_;
    StatusValue<IPAddress> subnetMask_;
    StatusValue<IPAddress> deviceGateway_;
    StatusValue<std::string> macAddress_;
};

extern NetworkStatus networkStatus;

#endif