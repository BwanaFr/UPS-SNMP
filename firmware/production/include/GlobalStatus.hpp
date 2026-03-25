#ifndef _GLOBALSTATUS_HPP__
#define _GLOBALSTATUS_HPP__

#include <StatusProvider.hpp>
#include <Arduino.h>
#include <FreeRTOS.h>
#include "driver/temperature_sensor.h"

/**
 * Device global status
 */
class GlobalStatus : public StatusProvider {
public:
    /**
     * Default constructor
     */
    GlobalStatus();

    /**
     * Default destructor
     */
    virtual ~GlobalStatus() = default;

    /**
     * Starts global status task
     */
    void begin();

    /**
     * Gets CPU internal temperature
     */
    float getInternalTemperature();

private:
    StatusValue<std::string> deviceDesc_;
    StatusValue<float> internalTemperature_;
    StatusValue<TimeTicks> upTime_;
    StatusValue<size_t> freeHeap_;
    StatusValue<std::string> physicalDesc_;
    StatusValue<std::string> swVersion_;
    StatusValue<std::string> compileTime_;
    StatusValue<std::string> resetReason_;
    temperature_sensor_handle_t tempHandle;
    void refreshStatus();
    static void statusTask(void* param);
    void getResetReason();
};

extern GlobalStatus globalStatus;

#endif