#ifndef _PINGER_H__
#define _PINGER_H__

#include <cstdint>
#include <FreeRTOS.h>
#include <atomic>
#include <ETH.h>
#include <StatusProvider.hpp>

class Pinger : public StatusProvider {
public:
    /**
     * Constructor
     */
    Pinger();
    virtual ~Pinger();

    /**
     * Starts to ping gateway
     */
    void start();

    /**
     * Stops pinging gateway
     */
    void stop();

    /**
     * Sets ping timeout value (ms)
     * @param ms Ping timeout milliseconds
     */
    void setTimeout(int ms);

    /**
     * Gets timeout
     * @return Ping timeout in milliseconds
     */
    int getTimeout();

    /**
     * Sets ping period
     * @param ms Milliseconds between pings
     */
    void setPingPeriod(int ms);

    /**
     * Gets ping period
     * @return Delay between ping [ms]
     */
    int getPingPeriod();

    /**
     * Configure this
     * @param averageTime Ping time (ms)
     * @param pingOk True if ping is ok
     * @param period Ping period in ms
     * @param timeout Ping timeout in ms
     */
    void configure(bool pingGateway, const IPAddress& ip, int period, int timeout);

    /**
     * Gets ping result
     * @param averageTime Ping time (ms)
     * @param pingOk True if ping is ok
     * @return True if ping is valid (IP configured properly)
     */
    bool getResult(bool& pingOk, float& averageTime);

    /**
     * Sets IP address to be pinged
     * @param pingGateway True to ping gateway
     * @param ip IP address to ping (0.0.0.0) to disable
     */
    void setIPToPing(bool pingGateway, const IPAddress& ip);

private:
    static void pingerTask(void* param);
    SemaphoreHandle_t mutexData_;           //!<< Protect access to data
    StatusValue<bool> pingOk_;              //!<< Ping result
    StatusValue<float> pingAvgTime_;        //!<< Ping average time
    bool pingGateway_;                      //!<< True to automatically ping gateway
    std::atomic<bool> started_;             //!<< Is task started
    int timeOutMs_;                         //!<< Ping timeout
    int pingPeriodMs_;                      //!<< Delay between pings
    void pingerLoop();                      //!<< Called by pingerTask
    TaskHandle_t pingTask_;                 //!<< Handle to the task
    IPAddress customIPToPing_;              //!<< Custom IP to ping (if pingGateway_ is false)
};

extern Pinger pinger;

#endif
