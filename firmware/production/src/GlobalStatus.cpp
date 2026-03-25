#include <GlobalStatus.hpp>

#include "esp_log.h"
#include <FreeRTOS.h>
#include <ETH.h>
#include <Configuration.hpp>
#include <GitVersion.hpp>

static const char *TAG = "GlobalStatus";

#ifndef TEMP_READ_PERIOD
#define TEMP_READ_PERIOD 500
#endif


GlobalStatus::GlobalStatus() :
    StatusProvider("System"),
    deviceDesc_(this, "Name", nullptr, [](){ std::string devName; configuration.getDeviceName(devName); return devName;}),
    internalTemperature_(this, "CPU Temp", "C", 0.0f),
    upTime_(this, "Uptime", [](){ return (esp_timer_get_time() / 10000); }),      //System uptime in 1/100s
    freeHeap_(this, "Free", "kB", [](){ return heap_caps_get_free_size(MALLOC_CAP_DEFAULT) / 1024; }),
    physicalDesc_(this, "PhysicalDescr", nullptr, "Waveshare module"),
    swVersion_(this, "Version", nullptr, GIT_VERSION),
    compileTime_(this, "Compile date", nullptr, __DATE__ " " __TIME__),
    resetReason_(this, "Reset reason", nullptr),
    tempHandle(NULL)
{
    deviceDesc_.setSNMPOID("1.3.6.1.2.1.1.1");
    deviceDesc_.setDisplayableOnScreen(false);
    internalTemperature_.setSNMPOID("1.3.6.1.4.1.119.5.1.2.1.5.1");
    upTime_.setSNMPOID("1.3.6.1.2.1.1.3");
    physicalDesc_.setSNMPOID("1.3.6.1.2.1.47.1.1.1.1.2");
    physicalDesc_.setDisplayableOnScreen(false);
    compileTime_.setDisplayableOnScreen(false);
    temperature_sensor_config_t temp_sensor = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    temperature_sensor_install(&temp_sensor, &tempHandle);
    getResetReason();
}

void GlobalStatus::begin()
{
    xTaskCreatePinnedToCore(statusTask, "statusTask", 2048, (void*)this, tskIDLE_PRIORITY + 1, NULL, ESP_TASK_MAIN_CORE ? 0 : 1);
}

float GlobalStatus::getInternalTemperature()
{
    return internalTemperature_.getValue();
}

void GlobalStatus::refreshStatus()
{
    temperature_sensor_enable(tempHandle);
    for(;;){
        //Read internal temperature
        float intTemp = 0.0;
        temperature_sensor_get_celsius(tempHandle, &intTemp);

        //Sets value
        internalTemperature_.setValue(intTemp);
        upTime_.updated();
        freeHeap_.updated();

        //Sleep before refreshing
        delay(500);
    }
    temperature_sensor_disable(tempHandle);
}

void GlobalStatus::statusTask(void* param)
{
    GlobalStatus* app = static_cast<GlobalStatus*>(param);
    app->refreshStatus();
}

void GlobalStatus::getResetReason()
{
    esp_reset_reason_t reason = esp_reset_reason();
    switch(reason){
        case ESP_RST_POWERON:
            resetReason_.setValue("power-on");
            resetReason_.setDisplayableOnScreen(false);
            break;
        case ESP_RST_EXT:
            resetReason_.setValue("reset-pin");
            resetReason_.setDisplayableOnScreen(false);
            break;
        case ESP_RST_SW:
            resetReason_.setValue("esp_restart");
            resetReason_.setDisplayableOnScreen(false);
            break;
        case ESP_RST_PANIC:
            resetReason_.setValue("panic");
            break;
        case ESP_RST_INT_WDT:
            resetReason_.setValue("intr_wd");
            break;
        case ESP_RST_TASK_WDT:
            resetReason_.setValue("task_wd");
            break;
        case ESP_RST_WDT:
            resetReason_.setValue("watchdog");
            break;
        case ESP_RST_DEEPSLEEP:
            resetReason_.setValue("watchdog");
            break;
        case ESP_RST_BROWNOUT:
            resetReason_.setValue("brownout");
            break;
        case ESP_RST_SDIO:
            resetReason_.setValue("sdio");
            break;
        case ESP_RST_USB:
            resetReason_.setValue("usb");
            break;
        case ESP_RST_JTAG:
            resetReason_.setValue("jtag");
            break;
        case ESP_RST_EFUSE:
            resetReason_.setValue("efuse");
            break;
        case ESP_RST_PWR_GLITCH:
            resetReason_.setValue("pwr_glitch");
            break;
        case ESP_RST_CPU_LOCKUP:
            resetReason_.setValue("cpu_lock_up");
            break;
        default:
            resetReason_.setValue("unknown");
            break;
    }
}

GlobalStatus globalStatus;
