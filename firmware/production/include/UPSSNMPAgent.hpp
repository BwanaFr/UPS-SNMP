#ifndef _UPS_SNMP_AGENT_HPP__
#define _UPS_SNMP_AGENT_HPP__
#include <Arduino.h>
#include <SNMP.h>
#include <FreeRTOS.h>
#include <vector>
#include <StatusProvider.hpp>
#include <string>

#define MAX_SNMP_PACKET_LEN 1400

class UPSSNMPAgent
{
public:
    /**
     * Constructor
     */
    UPSSNMPAgent();
    virtual ~UPSSNMPAgent();

    /**
     * Begin method
     */
    void begin();

    /**
     * Starts listening
     */
    void start();

    /**
     * Stops listening
     */
    void stop();

    /**
     * Sets SNMP community
     */
    void setSNMPCommunity(const std::string& comString);
private:

    /**
     * Insert OID from statusprovider into response
     * @param oid OID to locate
     * @param response SNMP message to put BER into
     * @return true if OID is located
     */
    static bool insertIntoResponse(const char* oid, SNMP::Message* response);

    /**
     * SNMP task loop function
     */
    void snmpTask();

    /**
     * Process SNMP packet from socket
     */
    void processSNMPPacket();

    /**
     * Creates the control socket used for starting/stopping the service\
     * @return The socket file descriptor or negative if error
     */
    int createControlSocket();

    /**
     * Closes the control socket
     */
    void closeControlSocket();

    /**
     * Send data to control socket
     * @param data Pointer to data to be sent
     * @param dataLen number of bytes contained in data
     * @return Number of bytes sent to the control socket, or negative if error
     */
    int sendToControlSocket(const void* data, std::size_t dataLen);

    /**
     * Receive data from control socket
     * @param data Pointer to received data buffer
     * @param dataLen Size of the data buffer
     * @return Number of bytes received from the control socket, or negative if error
     */
    int receiveFromControlSocket(void* data, std::size_t dataLen);

    /**
     * Creates the socket listening on SNMP port
     * @return The socket file descriptor or negative if error
     */
    int createListenSocket();

    /**
     * Close the socket used for listening
     */
    void closeListenSocket();

    static constexpr uint8_t CTRL_NEW_COMMUNITY = 0x2;  //!< New community string
    static constexpr uint8_t CTRL_SHUTDOWN = 0.3;       //!< Shutdown all

    int ctrl_fd_;                           //!< Control socket file descriptor
    int fd_;                                //!< UDP socket file descriptor
    TaskHandle_t taskHandle_;               //!< FreeRTOS task handle
    uint8_t buffer_[MAX_SNMP_PACKET_LEN];   //!< Temporary buffer
    std::string community_;                 //!< Community string
};
#endif