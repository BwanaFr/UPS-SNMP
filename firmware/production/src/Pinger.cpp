#include <Pinger.hpp>
#include <ESP32Ping.h>
#include <ETH.h>
#include "esp_log.h"

static const char* TAG = "Pinger";

Pinger::Pinger() :
            StatusProvider("Ping"),
            pingOk_(this, "Ok", false),
            pingAvgTime_(this, "Time", "ms", 0.0f),
            pingGateway_(true),
            started_(false),
            timeOutMs_(50), pingPeriodMs_(1000), pingTask_(NULL),
            customIPToPing_(INADDR_NONE)
{
    mutexData_ = xSemaphoreCreateMutex();
    if(mutexData_ == NULL){
        ESP_LOGE(TAG, "Unable to create data mutex");
    }
    pingOk_.setAlarmText("Ping not ok");
}

Pinger::~Pinger()
{
    if(mutexData_){
        vSemaphoreDelete(mutexData_);
        mutexData_ = nullptr;
    }
}

void Pinger::start()
{
    if(pingTask_ == NULL){
        xTaskCreatePinnedToCore(pingerTask, "pingTask", 4096, (void*)this, tskIDLE_PRIORITY + 2, &pingTask_, ESP_TASK_MAIN_CORE ? 0 : 1);
    }
    started_ = true;
    pingOk_.setValue(false);
}

void Pinger::stop()
{
    started_ = false;
    pingOk_.setValue(false);
    pingOk_.clearAlarm();
    //TODO: Add vTaskSuspend to remove started and not consume CPU
}

void Pinger::setTimeout(int ms)
{
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        timeOutMs_ = ms;
        xSemaphoreGive(mutexData_);
    }
}

int Pinger::getTimeout()
{
    int ret = 0;
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        ret = timeOutMs_;
        xSemaphoreGive(mutexData_);
    }
    return ret;
}

void Pinger::setPingPeriod(int ms)
{
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        pingPeriodMs_ = ms;
        xSemaphoreGive(mutexData_);
    }
}

int Pinger::getPingPeriod()
{
    int ret = 0;
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        ret = pingPeriodMs_;
        xSemaphoreGive(mutexData_);
    }
    return ret;
}

void Pinger::configure(bool pingGateway, const IPAddress& ip, int period, int timeout)
{
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        pingPeriodMs_ = period;
        timeOutMs_ = timeout;
        xSemaphoreGive(mutexData_);
    }
    setIPToPing(pingGateway, ip);
}

void Pinger::setIPToPing(bool pingGateway, const IPAddress& ip)
{
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        pingGateway_ = pingGateway;
        customIPToPing_ = ip;
        xSemaphoreGive(mutexData_);
    }
    if(pingGateway || (ip != INADDR_NONE)){
        setEnabled(true);
    }else{
        //No ping active, hide dependant data
        setEnabled(false);
    }
}

bool Pinger::getResult(bool& pingOk, float& averageTime)
{
    IPAddress pingIP = INADDR_NONE;
    averageTime = pingAvgTime_.getValue();
    pingOk = pingOk_.getValue();
    pingIP = pingGateway_ ?  ETH.gatewayIP() : customIPToPing_;
    return pingIP != INADDR_NONE;
}

void Pinger::pingerTask(void* param)
{
    ((Pinger*)param)->pingerLoop();
}

void Pinger::pingerLoop()
{
    while (true)
    {
        //Wait for pinger to be started
        //TODO: Maybe use RTOS Task Notifications, or just a suspended task
        while(!started_.load()){
            //Sleep 50ms
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }

        while(started_.load()){
            int timeout, period;
            IPAddress pingIP = INADDR_NONE;
            if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
            {
                period = pingPeriodMs_;
                timeout = timeOutMs_;
                pingIP = pingGateway_ ?  ETH.gatewayIP() : customIPToPing_;
                xSemaphoreGive(mutexData_);
            }
            if(pingIP != INADDR_NONE){
                //Something to ping
                bool pingok = Ping.pingMs(pingIP, 1, timeout);
                pingOk_.setValue(pingok);
                pingAvgTime_.setValue(Ping.averageTime());
                if(!pingok){
                    pingOk_.setAlarm(AlarmSeverity::ERROR);
                }else{
                    pingOk_.setAlarm(AlarmSeverity::INACTIVE);
                }
            }else{
                pingOk_.clearAlarm();
            }
            if(isOnScreen()){
                vTaskDelay(1000 / portTICK_PERIOD_MS);
            }else{
                vTaskDelay(period / portTICK_PERIOD_MS);
            }
        }
    }
}

Pinger pinger;