#ifndef _TEMPERATURE_HPP__
#define _TEMPERATURE_HPP__

#ifdef HAS_TEMP_PROBE
#include <StatusProvider.hpp>
#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

class TemperatureProbe : public StatusProvider {
public:
    TemperatureProbe(uint8_t pin);
    virtual ~TemperatureProbe() = default;
    void begin();
    double getTemperatureProbe();
    inline const StatusCollection* getStatus() const override { return &statusCollection_; };
private:
    OneWire wire_;
    DallasTemperature dallas_;
    StatusValue<double> temperature_;
    StatusCollection statusCollection_;
    uint32_t failureCount_;
    void readTemperature();
    static void temperatureTask(void* param);
};

extern TemperatureProbe tempProbe;
#endif

#endif