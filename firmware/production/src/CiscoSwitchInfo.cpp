#include <CiscoSwitchInfo.hpp>
#include <esp_log.h>
#include <SNMP.h>
#include <Arduino.h>
#include <sstream>
#include <cmath>
#include <ETH.h>
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#define SNMP_PORT 161

static const char* TAG = "CiscoSwitchInfo";

CiscoSensor::CiscoSensor() : physicalIndex{-1}, name{""}, type{DataType::NONE}, precision{0}, scale{DataScale::NONE}, value{0.0},
                            statusValue{nullptr}
{
}

CiscoSensor::~CiscoSensor()
{
    destroyStatusProvider();
}

void CiscoSensor::setValue(int newValue)
{
    value = newValue * std::pow(10, -1*precision);
    if(statusValue){
        statusValue->setValue(value);
    }
}

void CiscoSensor::computeUnit()
{
    std::ostringstream oss;
    switch(scale){
        case DataScale::YOCTO:    oss << "y"; break;
        case DataScale::ZEPTO:    oss << "z"; break;
        case DataScale::ATTO:     oss << "a"; break;
        case DataScale::FEMTO:    oss << "f"; break;
        case DataScale::PICO:     oss << "p"; break;
        case DataScale::NANO:     oss << "n"; break;
        case DataScale::MICRO:    oss << "u"; /*TODO: Unicode?*/ break;
        case DataScale::MILLI:    oss << "m"; break;
        case DataScale::KILO:     oss << "k"; break;
        case DataScale::MEGA:     oss << "M"; break;
        case DataScale::GIGA:     oss << "G"; break;
        case DataScale::TERA:     oss << "T"; break;
        case DataScale::EXA:      oss << "E"; break;
        case DataScale::PETA:     oss << "P"; break;
        case DataScale::ZETTA:    oss << "Z"; break;
        case DataScale::YOTTA:    oss << "Y"; break;
    }
    switch(type){
        case DataType::VOLTSAC:   oss << "Vac"; break;
        case DataType::VOLTSDC:   oss << "Vdc"; break;
        case DataType::AMPERES:   oss << "A";   break;
        case DataType::WATTS:     oss << "W";   break;
        case DataType::HERTZ:     oss << "Hz";  break;
        case DataType::CELSIUS:   oss << "C";   break;
        case DataType::PERCENTRH: oss << "%Rh"; break;
        case DataType::RPM:       oss << "rpm"; break;
        case DataType::CMM:       oss << "cmm"; break;
        case DataType::DBM:       oss << "dBm"; break;
        default:                  oss << "?"; break;
    }
    unit = oss.str();
}

void CiscoSensor::setDataScale(DataScale newScale)
{
    scale = newScale;
    getUnit();
}

void CiscoSensor::setDataType(DataType newType)
{
    type = newType;
    getUnit();
}

void CiscoSensor::installStatusProvider(StatusProvider* provider)
{
    if(!statusValue && (physicalIndex > 0)){
        statusValue = new  StatusValue<double>(provider, "RX power", &unit[0]);
        statusValue->setValue(value);
    }
}

void CiscoSensor::destroyStatusProvider()
{
    if(statusValue){
        delete statusValue;
        statusValue = nullptr;
    }
}

CiscoSwitchInterface::CiscoSwitchInterface(int index) : interfaceIndex(index), physicalIndex{0}, trunk{false},
                    statusProvider{nullptr}, cdpNeighbour{nullptr}, ifAlias{nullptr}
{
}

CiscoSwitchInterface::CiscoSwitchInterface(int index, bool isTrunk) : interfaceIndex(index), physicalIndex{0}, trunk{isTrunk},
                    statusProvider{nullptr}, cdpNeighbour{nullptr}, ifAlias{nullptr}
{
}


void CiscoSwitchInterface::setName(const char* name)
{
    this->name = name;
}

void CiscoSwitchInterface::setCDPNeighbour(const char* neighbour)
{
    this->neighbour = neighbour;
    if(cdpNeighbour){
        cdpNeighbour->setValue(neighbour);
    }
}

void CiscoSwitchInterface::setAlias(const char* alias)
{
    this->alias = alias;
    if(ifAlias){
        ifAlias->setValue(this->alias);
    }
}

void CiscoSwitchInterface::installStatusProvider()
{
    if(!statusProvider){
        statusProvider = new StatusProvider(&name[0]);
        cdpNeighbour = new StatusValue<std::string>(statusProvider, "CDP");
        cdpNeighbour->setValue(neighbour);
        rxPowerSensor.installStatusProvider(statusProvider);
        mode = new StatusEnumValue(statusProvider, "Mode", {{0, "access"}, {1, "trunk"}});
        mode->setValue(trunk);
        ifAlias = new StatusValue<std::string>(statusProvider, "Alias");
        ifAlias->setValue(alias);
    }
}

void CiscoSwitchInterface::destroyStatusProvider()
{
    if(statusProvider){
        delete statusProvider;
        statusProvider = nullptr;
        delete cdpNeighbour;
        cdpNeighbour = nullptr;
        rxPowerSensor.destroyStatusProvider();
        delete mode;
        mode = nullptr;
        delete ifAlias;
        ifAlias = nullptr;
    }
}

CiscoSwitchInfoFetcher::CiscoSwitchInfoFetcher() :
    started_{false}, timeout_{10000}, switchAddr_{0,0,0,0}, taskHandle_{nullptr},
    snmpFailures_{0}, dirty_{true}, buffer_{0}
{}

CiscoSwitchInfoFetcher::~CiscoSwitchInfoFetcher()
{
}

void CiscoSwitchInfoFetcher::start()
{
    if(!started_ && (switchAddr_.operator uint32_t() != 0) && ETH.hasIP()){
        started_ = true;
        if(taskHandle_ == nullptr){
            xTaskCreatePinnedToCore(taskFunction, "CiscoFetcher", 4096, (void*)this, tskIDLE_PRIORITY + 1, &taskHandle_, ESP_TASK_MAIN_CORE ? 0 : 1);
        }else{
            vTaskResume(taskHandle_);
        }
    }
}

void CiscoSwitchInfoFetcher::stop()
{
    if(started_){
        started_ = false;
        vTaskSuspend(taskHandle_);
    }
}

void CiscoSwitchInfoFetcher::setSwitchIPAddress(const IPAddress& ip)
{
    if(ip != switchAddr_){
        dirty_ = true;
    }
    switchAddr_ = ip;
    if(ip.operator uint32_t() != 0){
        start();
    }else{
        stop();
    }
}

const SNMP::Message* CiscoSwitchInfoFetcher::sendRequest(SNMP::Message* msg)
{
    if(started_){
        SNMP::Message* ret = nullptr;

        uint8_t* tx_bufferPtr = buffer_;
        if(!msg->build(tx_bufferPtr, buffer_ + sizeof(buffer_))){
            ESP_LOGE(TAG, "Unable to build request message");
            return nullptr;
        }

        //Creates a socket to send/receive our SNMP data
        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(switchAddr_.toString().c_str());
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(SNMP_PORT);

        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            return nullptr;
        }
        // Set timeout
        struct timeval timeout;
        timeout.tv_sec = 10; //timeout_/1000;
        timeout.tv_usec = 0; //(timeout_ - timeout.tv_sec*1000) * 1000;
        setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

        std::size_t msgLen = tx_bufferPtr - buffer_;
        int err = sendto(sock, buffer_, msgLen, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
        }else{
            //Now receive
            struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
            socklen_t socklen = sizeof(source_addr);
            int len = recvfrom(sock, buffer_, sizeof(buffer_), 0, (struct sockaddr *)&source_addr, &socklen);
            if(len > 0){
                ret = new SNMP::Message();
                tx_bufferPtr = buffer_;
                if(!ret->parse(tx_bufferPtr, tx_bufferPtr + len)){
                    ESP_LOGE(TAG, "Unable to parse SNMP response from ID %d", msg->getRequestID());
#if 0
                    for(int i=0;i<len;++i){
                        log_printf("%02x ", buffer_[i]);
                    }
                    log_printf("\n");
#endif
                    delete ret;
                    ret = nullptr;
                }
            }else{
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
            }
        }

        if(ret && !dirty_){
            //Only reset if not dirty
            snmpFailures_ = 0;
        }
        if(!ret){
            ++snmpFailures_;
            if(snmpFailures_ >= MAX_FAILURES){
                dirty_ = true;
            }
        }

        if(sock >= 0){
            shutdown(sock, 0);
            close(sock);
        }
        return ret;
    }
    return nullptr;
}

void CiscoSwitchInfoFetcher::loop()
{
    static unsigned long lastQuery = millis();
    static unsigned long lastFastQuery = 0;

    //Gets if one of our interface is on screen
    CiscoSwitchInterface* onScreenIntf = getInterfaceOnScreen();
    if(onScreenIntf && !dirty_ && ((millis() - lastFastQuery) >= 2000)){
        //Found and valid, refresh data every 2 seconds
        refreshInterfaceInfo(onScreenIntf);
        lastFastQuery = millis();
    }
    if(dirty_ || ((millis() - lastQuery) > 10000)){ //TODO: Increase update frequency when on screen
        if(dirty_){
            if(onScreenIntf){
                //Interface is shown on screen, don't destroy it now
                delay(100);
                return;
            }
            //TODO: Do it only when transition to dirty
            for(InterfacesMap::iterator it = interfaces_.begin(); it != interfaces_.end(); ++it)
            {
                it->second.destroyStatusProvider();
            }
            ESP_LOGI(TAG, "Fetching ports information from %s", switchAddr_.toString().c_str());
            //Get infos
            snmpFailures_ = 0;
            interfaces_.clear();

            //Gets all interfaces configured as trunk
            getTrunkInterfaces();

            //Gets physical aliases of previously found interfaces
            if(snmpFailures_ == 0)
                getTrunkPhysicalAliases();

            //Find all interfaces with RX power sensor
            if(snmpFailures_ == 0)
                getRXMeasurableInterfaces();

            //Gets physical interfaces name
            if(snmpFailures_ == 0)
                getInterfacesNames();

            //Query successfully performed, clear dirty flag
            if(snmpFailures_ == 0){
                dirty_ = false;
                ESP_LOGI(TAG, "Found %u interresting ports", interfaces_.size());
                //Enable status provider for all detected interfaces
                for(InterfacesMap::iterator it = interfaces_.begin(); it != interfaces_.end(); ++it)
                {
                    it->second.installStatusProvider();
                }
            }
        }

        //Gets interfaces information
        for(InterfacesMap::iterator it = interfaces_.begin(); it != interfaces_.end(); ++it)
        {
            refreshInterfaceInfo(&it->second);
        }

        lastQuery = millis();
    }
    delay(10);
}

void CiscoSwitchInfoFetcher::refreshInterfaceInfo(CiscoSwitchInterface* intf)
{
    //Gets SFP RX power value
    if(intf->getRXPowerSensor()->getPhysicalIndex() >= 0){
        getSensorValue(intf->getRXPowerSensor());
    }

    //Gets interfaces neighbours using CDP cache
    getInterfaceCDP(intf);

}

void CiscoSwitchInfoFetcher::taskFunction(void* param)
{
    CiscoSwitchInfoFetcher* cls = static_cast<CiscoSwitchInfoFetcher*>(param);
    while(true){
        cls->loop();
    }
}

void CiscoSwitchInfoFetcher::getTrunkInterfaces()
{
    sendGetSubtree(vlanTrunkPortDynamicStatus, [this](const std::string& oid, const SNMP::BER* ber){
        if(ber->getType() == SNMP::Type::Integer){
            const SNMP::IntegerBER* intBER = static_cast<const SNMP::IntegerBER*>(ber);
            if(intBER->getValue() == TRUNKING){
                std::string  vlanTrunkPortIfIndexStr;
                getOIDIndex(oid, vlanTrunkPortIfIndexStr);
                int portIfIndex = std::atoi(vlanTrunkPortIfIndexStr.c_str());
                //Add it to map
                interfaces_.emplace(portIfIndex, CiscoSwitchInterface(portIfIndex, true));
            }
        }
    });
}

bool CiscoSwitchInfoFetcher::getSensorsTransceiverRxPwr(std::vector<int>* entries)
{
    return sendGetSubtree(entPhysicalVendorType, [entries](const std::string& oid, const SNMP::BER* ber){
        if(ber->getType() == SNMP::Type::ObjectIdentifier){
            const SNMP::ObjectIdentifierBER* oidBER = static_cast<const SNMP::ObjectIdentifierBER*>(ber);
            if(strcmp(oidBER->getValue(), cevSensorTransceiverRxPwr) == 0){
                std::string  indexStr;
                getOIDIndex(oid, indexStr);
                int physIndex = std::atoi(indexStr.c_str());
                //Add it to return
                entries->push_back(physIndex);
            }
        }
    });
}

bool CiscoSwitchInfoFetcher::getPhysicalContainedIn(int entity, int* container)
{
    return sendGetRequest(entPhysicalContainedIn, entity, [container](const SNMP::BER* ber){
        if(ber->getType() == SNMP::Type::Integer){
            const SNMP::IntegerBER* intBER = static_cast<const SNMP::IntegerBER*>(ber);
            *container = intBER->getValue();
        }
    });
}

void CiscoSwitchInfoFetcher::getRXMeasurableInterfaces()
{
    //Gets all transceivers with RX power sensors
    std::vector<int> sfpContainers;
    getSensorsTransceiverRxPwr(&sfpContainers);
    for(int index : sfpContainers){
        int parentIndex = 0;
        int portIndex = 0;
        getPhysicalParentPort(index, parentIndex);
        getPhysicalAlias(parentIndex, &portIndex);
        InterfacesMap::iterator it = interfaces_.find(portIndex);
        if(it != interfaces_.end()){
            it->second.getRXPowerSensor()->setPhysicalIndex(index);
            getSensorConstants(it->second.getRXPowerSensor());
        }else{
            std::pair<InterfacesMap::iterator, bool> ret = interfaces_.emplace(portIndex, CiscoSwitchInterface{portIndex, false});
            if(ret.second){
                //Sets the physical index
                ret.first->second.setPhysicalIndex(parentIndex);
                ret.first->second.getRXPowerSensor()->setPhysicalIndex(index);
                getSensorConstants(ret.first->second.getRXPowerSensor());
            }
        }
    }
}

void CiscoSwitchInfoFetcher::getInterfacesNames()
{
    for(InterfacesMap::iterator it = interfaces_.begin(); it != interfaces_.end(); ++it){
        sendGetRequest(entPhysicalName, it->second.getPhysicalIndex(), [it](const SNMP::BER* ber){
            if(ber->getType() == SNMP::Type::OctetString){
                const SNMP::OctetStringBER* strBER = static_cast<const SNMP::OctetStringBER*>(ber);
                it->second.setName(strBER->getValue());
            }
        });
        getInterfaceAlias(&it->second);
    }
}

void CiscoSwitchInfoFetcher::getTrunkPhysicalAliases()
{
    sendGetSubtree(entPhysicalAliasOID, [this](const std::string& oid, const SNMP::BER* ber){
        if(ber->getType() == SNMP::Type::OctetString){
            const SNMP::OctetStringBER* strBER = static_cast<const SNMP::OctetStringBER*>(ber);
            std::string entPhysIndex;
            getOIDIndex(oid, entPhysIndex);
            if(strlen(strBER->getValue()) > 0){
                //Add to map only if the alias contains something
                int intfIndex = std::atoi(strBER->getValue());
                InterfacesMap::iterator intf = interfaces_.find(intfIndex);
                if(intf != interfaces_.end()){
                    intf->second.setPhysicalIndex(std::atoi(entPhysIndex.c_str()));
                }
            }
        }
    });
}

void CiscoSwitchInfoFetcher::getOIDIndex(const std::string& oid, std::string& index)
{
    std::size_t lastOf = oid.find_last_of('.');
    if(lastOf != std::string::npos){
        index = oid.substr(++lastOf);
    }
}

void CiscoSwitchInfoFetcher::getInterfaceCDP(CiscoSwitchInterface* interface)
{
    sendGetSubtree(cdpCacheDeviceId, interface->getInterfaceIndex(), [interface](const std::string& oid, const SNMP::BER* ber){
        if(ber->getType() == SNMP::Type::OctetString){
            const SNMP::OctetStringBER* strBER = static_cast<const SNMP::OctetStringBER*>(ber);
            interface->setCDPNeighbour(strBER->getValue());
        }
    });
}

void CiscoSwitchInfoFetcher::getInterfaceAlias(CiscoSwitchInterface* interface)
{
    sendGetRequest(ifAlias, interface->getInterfaceIndex(), [interface](const SNMP::BER* ber){
        if(ber->getType() == SNMP::Type::OctetString){
            const SNMP::OctetStringBER* valBER = static_cast<const SNMP::OctetStringBER*>(ber);
            interface->setAlias(valBER->getValue());
            ESP_LOGI(TAG, "Interface alias -> %s", valBER->getValue());
        }
    });
}

CiscoSwitchInfoFetcher::PhysicalClass CiscoSwitchInfoFetcher::getPhysicalClass(int index)
{
    PhysicalClass ret = PhysicalClass::NONE;
    PhysicalClass* retPtr = &ret;
    sendGetRequest(entPhysicalClass, index, [retPtr](const SNMP::BER* ber){
        if(ber->getType() == SNMP::Type::Integer){
            const SNMP::IntegerBER* intBER = static_cast<const SNMP::IntegerBER*>(ber);
            *retPtr = static_cast<PhysicalClass>(intBER->getValue());
        }
    });
    return ret;
}

bool CiscoSwitchInfoFetcher::getPhysicalParentPort(int entity, int& portEntity)
{
    int index = entity;
    while(true){
        int containedIn = 0;
        getPhysicalContainedIn(index, &containedIn);

        PhysicalClass cls = getPhysicalClass(containedIn);
        if(cls == PhysicalClass::PORT){
            portEntity = containedIn;
            return true;
        }else if(cls == PhysicalClass::NONE){
            portEntity = -1;
            break;
        }
    }
    return false;
}

bool CiscoSwitchInfoFetcher::getPhysicalAlias(int entity, int* alias)
{
    *alias = -1;
    return sendGetRequest(entPhysicalAliasOID, entity, [alias](const SNMP::BER* ber){
        if(ber->getType() == SNMP::Type::OctetString){
            const SNMP::OctetStringBER* valBER = static_cast<const SNMP::OctetStringBER*>(ber);
            *alias = std::atoi(valBER->getValue());
        }
    });
}

bool CiscoSwitchInfoFetcher::getSensorValue(CiscoSensor* sensor)
{
    return sendGetRequest(entSensorValue, sensor->getPhysicalIndex(), [sensor](const SNMP::BER* ber){
        if(ber->getType() == SNMP::Type::Integer){
            const SNMP::IntegerBER* valBER = static_cast<const SNMP::IntegerBER*>(ber);
            sensor->setValue(valBER->getValue());
        }
    });
}

void CiscoSwitchInfoFetcher::getSensorConstants(CiscoSensor* sensor)
{
    sendGetRequest(entSensorScale, sensor->getPhysicalIndex(), [sensor](const SNMP::BER* ber){
        if(ber->getType() == SNMP::Type::Integer){
            const SNMP::IntegerBER* valBER = static_cast<const SNMP::IntegerBER*>(ber);
            sensor->setDataScale(static_cast<CiscoSensor::DataScale>(valBER->getValue()));
        }
    });
    sendGetRequest(entSensorType, sensor->getPhysicalIndex(), [sensor](const SNMP::BER* ber){
        if(ber->getType() == SNMP::Type::Integer){
            const SNMP::IntegerBER* valBER = static_cast<const SNMP::IntegerBER*>(ber);
            sensor->setDataType(static_cast<CiscoSensor::DataType>(valBER->getValue()));
        }
    });
    sendGetRequest(entSensorPrecision, sensor->getPhysicalIndex(), [sensor](const SNMP::BER* ber){
        if(ber->getType() == SNMP::Type::Integer){
            const SNMP::IntegerBER* valBER = static_cast<const SNMP::IntegerBER*>(ber);
            sensor->setPrecision(valBER->getValue());
        }
    });
    sensor->computeUnit();
}

bool CiscoSwitchInfoFetcher::sendGetRequest(const char* oid, std::function<void(const SNMP::BER*)> cb)
{
    SNMP::Message msg(SNMP::Version::V2C, community_.c_str(), SNMP::Type::GetRequest);
    msg.add(oid, new SNMP::NullBER);
    const SNMP::Message* message = sendRequest(&msg);
    if(message){
        SNMP::VarBindList *varbindlist = message->getVarBindList();
        for (unsigned int index = 0; index < varbindlist->count(); ++index) {
            // Each variable binding is a sequence of 2 objects:
            // - First one is and ObjectIdentifierBER. It holds the OID
            // - Second is the value of any type
            SNMP::VarBind *varbind = (*varbindlist)[index];
            // There is a convenient function to get the OID as a const char*
            std::string name = varbind->getName();
            SNMP::BER* ber = varbind->getValue();
            if(name == oid){
                if(cb){
                    cb(ber);
                    delete message;
                    return true;
                }
            }
        }
        delete message;
    }else{
        ESP_LOGI(TAG, "SNMP timeout #%d, giving up", snmpFailures_);
    }
    return false;
}

bool CiscoSwitchInfoFetcher::sendGetRequest(const char* oid, int index, std::function<void(const SNMP::BER*)> cb)
{
    std::ostringstream oss;
    oss << oid << '.' << index;
    return sendGetRequest(oss.str().c_str(), cb);
}

bool CiscoSwitchInfoFetcher::sendGetSubtree(const char* oid, std::function<void(const std::string& oid, const SNMP::BER*)> cb)
{
    bool exit = false;
    std::string nextOID = oid;
    while(!exit){
        SNMP::Message msg(SNMP::Version::V2C, community_.c_str(), SNMP::Type::GetNextRequest);
        msg.add(nextOID.c_str(), new SNMP::NullBER);
        const SNMP::Message* message = sendRequest(&msg);
        if(message){
            SNMP::VarBindList *varbindlist = message->getVarBindList();
            for (unsigned int index = 0; index < varbindlist->count(); ++index) {
                // Each variable binding is a sequence of 2 objects:
                // - First one is and ObjectIdentifierBER. It holds the OID
                // - Second is the value of any type
                SNMP::VarBind *varbind = (*varbindlist)[index];
                // There is a convenient function to get the OID as a const char*
                std::string name = varbind->getName();
                SNMP::BER* ber = varbind->getValue();
                if(name.rfind(oid, 0) == 0){
                    if(cb){
                        cb(name, ber);
                    }
                    nextOID = name;
                }else{
                    exit = true;
                }
            }
            delete message;
        }else{
            ESP_LOGI(TAG, "SNMP timeout #%d, giving up", snmpFailures_);
            return false;
        }
    }
    return true;
}

bool CiscoSwitchInfoFetcher::sendGetSubtree(const char* oid, int index, std::function<void(const std::string& oid, const SNMP::BER*)> cb)
{
    std::ostringstream oss;
    oss << oid << '.' << index;
    return sendGetSubtree(oss.str().c_str(), cb);
}

CiscoSwitchInterface* CiscoSwitchInfoFetcher::getInterfaceOnScreen()
{
    for(InterfacesMap::iterator it = interfaces_.begin(); it != interfaces_.end(); ++it)
    {
        if(it->second.isOnScreen()){
            return &it->second;
        }
    }
    return nullptr;
}

CiscoSwitchInfoFetcher ciscoFetcher;