#include <UPSSNMPAgent.hpp>
#include <Arduino.h>
#include <esp_log.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "sdkconfig.h"

//UDP control port
#define SNMP_CTRL_PORT 16161
#define SNMP_AGENT_PORT 161

static const char* TAG = "SNMPAgent";


UPSSNMPAgent::UPSSNMPAgent() : ctrl_fd_(-1), fd_(-1), taskHandle_{nullptr}, buffer_{0}
{
}

UPSSNMPAgent::~UPSSNMPAgent()
{
    if(ctrl_fd_ > -1){
        close(ctrl_fd_);
    }
    if(fd_ > -1){
        close(fd_);
    }
}

void UPSSNMPAgent::begin()
{
}

void UPSSNMPAgent::start()
{
    //Starts our FreeRTOS task
    if(taskHandle_ == nullptr){
        ESP_LOGI(TAG, "Starting SNMP agent");
        xTaskCreatePinnedToCore([](void* instance)-> void {
            static_cast<UPSSNMPAgent*>(instance)->snmpTask();
        }, "SNMPAgent", 4096, (void*)this, tskIDLE_PRIORITY + 1, &taskHandle_, ESP_TASK_MAIN_CORE ? 0 : 1);
    }
}

void UPSSNMPAgent::stop()
{
    if(taskHandle_){
        ESP_LOGI(TAG, "Stopping SNMP agent");
        uint8_t msg = CTRL_SHUTDOWN;
        sendToControlSocket(&msg, sizeof(uint8_t));
    }
}

void UPSSNMPAgent::setSNMPCommunity(const std::string& comString)
{
    community_ = comString;
    if(taskHandle_){
        //Send control message to take into account new community
        uint8_t msg = CTRL_NEW_COMMUNITY;
        sendToControlSocket(&msg, sizeof(uint8_t));
    }
}

void UPSSNMPAgent::snmpTask()
{
    int ret = createControlSocket();
    if(ret >= 0){
        while(true){
            //Create listen socket if needed
            createListenSocket();
            //Create set to use select on it
            fd_set readSet;
            FD_ZERO(&readSet);
            if(fd_ > -1){
                FD_SET(fd_, &readSet);
            }
            FD_SET(ctrl_fd_, &readSet);
            int maxFd = std::max(ctrl_fd_, fd_);
            int cnt = select(maxFd + 1, &readSet, NULL, NULL, NULL);
            if(cnt < 0){
                ESP_LOGE(TAG, "error in select (%d)", errno);
                vTaskDelay(100 / portTICK_PERIOD_MS);
                continue;
            }

            if((fd_ > -1) && FD_ISSET(fd_, &readSet)){
                //Data received on UDP socket
                processSNMPPacket();
            }

            if(FD_ISSET(ctrl_fd_, &readSet)){
                uint8_t msg = 0;
                if(receiveFromControlSocket(&msg, sizeof(uint8_t))){
                    switch(msg){
                        case CTRL_SHUTDOWN:
                            goto shutdown;
                        case CTRL_NEW_COMMUNITY:
                            if(community_.empty()){
                                //Stops listening
                                closeListenSocket();
                            }
                            break;
                    }
                }
            }
        }
    }
shutdown:
//Close all sockets and kill our FreeRTOS task
    closeControlSocket();

    closeListenSocket();

    taskHandle_ = nullptr;
    vTaskDelete(NULL);
}

bool UPSSNMPAgent::insertIntoResponse(const char* oid, SNMP::Message* response)
{
    const StatusData* data = StatusProvider::locateSNMPData(oid);
    if(data){
        SNMP::BER *ber = data->buildSNMPBER();
        if(ber){
            response->add(oid, ber);
            return true;
        }
    }
    return false;
}

void UPSSNMPAgent::processSNMPPacket()
{
    struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
    socklen_t socklen = sizeof(source_addr);
    int len = recvfrom(fd_, buffer_, sizeof(buffer_), 0, (struct sockaddr *)&source_addr, &socklen);
    if(len < 0){
        ESP_LOGE(TAG, "Unable to recvfrom SNMP packet. (%d)", len);
        return;
    }

    //Parse our SNMP message from buffer
    SNMP::Message message;
    uint8_t* rxPointer = buffer_;
    const uint8_t* rxPointerEnd = rxPointer + len;
    if(!message.parse(rxPointer, rxPointerEnd)){
        ESP_LOGE(TAG, "Unable to parse SNMP packet!");
        return;
    }

    //Check community
    if(!community_.empty() && (community_ != message.getCommunity())){
        // Get the sender's ip address as string (only  for debug, remove me!)
        char addr_str[128];
        if (source_addr.ss_family == PF_INET) {
            inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
        } else if (source_addr.ss_family == PF_INET6) {
            inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
        }
        ESP_LOGE(TAG, "Wrong SNMP community (requested %s from %s)!", message.getCommunity(), addr_str);
        return;
    }

    // Create an SNMP message for response
    SNMP::Message response(message.getVersion(),
            message.getCommunity(), SNMP::Type::GetResponse);
    // The response must have the same request-id as the request
    response.setRequestID(message.getRequestID());

    bool send = false;
    switch(message.getType()){
        case SNMP::Type::GetRequest:
        {
            // Get the variable binding list from the message.
            SNMP::VarBindList *varbindlist = message.getVarBindList();
            for (unsigned int index = 0; index < varbindlist->count(); ++index) {
                // Each variable binding is a sequence of 2 objects:
                // - First one is and ObjectIdentifierBER. It holds the OID
                // - Second is the value of any type
                SNMP::VarBind *varbind = (*varbindlist)[index];
                // There is a convenient function to get the OID as a const char*
                const char *name = varbind->getName();
                send |= insertIntoResponse(name, &response);
            }
        }
            break;
        case SNMP::Type::GetNextRequest:
            SNMP::VarBindList *varbindlist = message.getVarBindList();
            const uint8_t count = varbindlist->count();
            for (uint8_t index = 0; index < count; ++index) {
                // Each variable binding is a sequence of 2 objects:
                // - First one is and ObjectIdentifierBER. It holds the OID
                // - Second is the value of any type
                SNMP::VarBind *varbind = (*varbindlist)[index];
                // There is a convenient function to get the OID as a const char*
                const char *name = varbind->getName();
                bool oidFound = false;
                const StatusData* data = StatusProvider::locateNextSNMPData(name, oidFound);
                if(!oidFound){
                    // OID is unknown
                    switch (message.getVersion()) {
                    case SNMP::Version::V1:
                        // Set error, status and index
                        response.setError(SNMP::Error::GenErr, index + 1);
                        // Add OID to response with null value;
                        response.add(name);
                    case SNMP::Version::V2C:
                        // No such object
                        response.add(name, new SNMP::NoSuchObjectBER());
                        break;
                    }
                }else if(!data){
                    //No next data
                    // This is the last OID of the MIB
                    switch (message.getVersion()) {
                    case SNMP::Version::V1:
                        // Set error, status and index
                        response.setError(SNMP::Error::NoSuchName, index + 1);
                        // Add OID to response with null value;
                        response.add(name);
                        break;
                    case SNMP::Version::V2C:
                        // End of MIB view
                        response.add(name, new SNMP::EndOfMIBViewBER());
                        break;
                    }
                }else{
                    //Next data found, put it in response
                    SNMP::BER *ber = data->buildSNMPBER();
                    if(ber){
                        response.add(data->getSNMPOID(), ber);
                    }
                }
                send |= true;
            }
            break;
    }
    if(send){
        uint8_t* tx_bufferPtr = buffer_;
        if(response.build(tx_bufferPtr, buffer_ + sizeof(buffer_))){
            std::size_t msgLen = tx_bufferPtr - buffer_;
            int err = sendto(fd_, buffer_, msgLen, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
            if (err < 0) {
                ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                return;
            }
        }else{
            ESP_LOGE(TAG, "Unable to build response message");
            return;
        }
    }
}

int UPSSNMPAgent::createControlSocket()
{
#if !CONFIG_LWIP_NETIF_LOOPBACK
    ESP_LOGE(TAG, "Please enable LWIP_NETIF_LOOPBACK for %s API", __func__);
    return -1;
#endif
//Create the control socket
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        ESP_LOGE(TAG, "Unable to create control socket!");
        return fd;
    }

//Bind socket to loopback
    struct sockaddr_storage addr = {};
    socklen_t addr_len = 0;
#if CONFIG_LWIP_IPV4
    struct sockaddr_in *addr4 = (struct sockaddr_in *)&addr;
    addr4->sin_family = AF_INET;
    addr4->sin_port = htons(SNMP_CTRL_PORT);
    inet_aton("127.0.0.1", &addr4->sin_addr);
    addr_len = sizeof(struct sockaddr_in);
#else
    struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&addr;
    addr6->sin6_family = AF_INET6;
    addr6->sin6_port = htons(SNMP_CTRL_PORT);
    inet6_aton("::1", &addr6->sin6_addr);
    addr_len = sizeof(struct sockaddr_in6);
#endif
    int ret = bind(fd, (struct sockaddr *)&addr, addr_len);
    if (ret < 0) {
        close(fd);
        ESP_LOGE(TAG, "Unable to bind control socket!");
        return ret;
    }
    ctrl_fd_ = fd;
    return ctrl_fd_;
}

void UPSSNMPAgent::closeControlSocket()
{
    if(ctrl_fd_ > -1){
        shutdown(ctrl_fd_, 0);
        close(ctrl_fd_);
        ctrl_fd_ = -1;
    }
}

int UPSSNMPAgent::sendToControlSocket(const void* data, std::size_t dataLen)
{
    int ret;
    struct sockaddr_storage to_addr = {};
    socklen_t addr_len = 0;
#if CONFIG_LWIP_IPV4
    struct sockaddr_in *addr4 = (struct sockaddr_in *)&to_addr;
    addr4->sin_family = AF_INET;
    addr4->sin_port = htons(SNMP_CTRL_PORT);
    inet_aton("127.0.0.1", &addr4->sin_addr);
    addr_len = sizeof(struct sockaddr_in);
#else
    struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&to_addr;
    addr6->sin6_family = AF_INET6;
    addr6->sin6_port = htons(SNMP_CTRL_PORT);
    inet6_aton("::1", &addr6->sin6_addr);
    addr_len = sizeof(struct sockaddr_in6);
#endif
    ret = sendto(ctrl_fd_, data, dataLen, 0, (struct sockaddr *)&to_addr, addr_len);

    if (ret < 0) {
        return -1;
    }
    return ret;
}

int UPSSNMPAgent::receiveFromControlSocket(void* data, std::size_t dataLen)
{
    int ret;
    ret = recvfrom(ctrl_fd_, data, dataLen, 0, NULL, NULL);
    if (ret < 0) {
        return -1;
    }
    return ret;
}

int UPSSNMPAgent::createListenSocket()
{
    if((fd_ == -1) && !community_.empty()){
        int ip_protocol = 0;
        struct sockaddr_in6 dest_addr;
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(SNMP_AGENT_PORT);
        ip_protocol = IPPROTO_IP;

        int sock = socket(AF_INET, SOCK_DGRAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            return sock;
        }
        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
            close(sock);
            return err;
        }
        fd_ = sock;
    }
    return fd_;
}

void UPSSNMPAgent::closeListenSocket()
{
    if(fd_ > -1){
        shutdown(fd_, 0);
        close(fd_);
        fd_ = -1;
    }
}
