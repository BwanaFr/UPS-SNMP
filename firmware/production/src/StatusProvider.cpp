#include <StatusProvider.hpp>
#include "esp_log.h"
#include <iomanip>

#if defined __has_include
#  if __has_include (<Customization.hpp>)
#    include <Customization.hpp>
#  endif
#endif

static const char *TAG = "StatusProvider";

StatusProvider::StatusProviders* StatusProvider::providers_ = nullptr;
SemaphoreHandle_t StatusProvider::providersMutex_ = nullptr;
bool StatusProvider::providerChanged_ = true;
StatusProvider::SNMPDataMap* StatusProvider::snmpData_ = nullptr;
StatusProvider::AlarmCallbacks* StatusProvider::alarmCallbacks_ = nullptr;


StatusProvider::StatusProvider(const char* name) : mutexData_{nullptr},
                                    firstData_{nullptr}, name_{name},
                                    enabled_{true}, onScreen_{false}
{
    if(!providers_){
        providers_  = new StatusProviders();
    }
    if(!providersMutex_){
        providersMutex_ = xSemaphoreCreateMutex();
        if(providersMutex_ == NULL){
            ESP_LOGE(TAG, "Unable to create providers mutex");
        }
    }

    mutexData_ = xSemaphoreCreateMutex();
    if(mutexData_ == NULL){
        ESP_LOGE(TAG, "Unable to create data mutex");
    }
    //Register this provider
    {
        DataLock lock(providersMutex_);
        providers_->push_back(this);
        providerChanged_ = true;
    }
}

StatusProvider::~StatusProvider()
{
    //Unregister this provider
    if(providersMutex_ && providers_){  //Excessive test
        DataLock lock(providersMutex_);
        providerChanged_ = true;
        for(StatusProviders::iterator it = providers_->begin(); it != providers_->end(); ++it){
            if(*it == this){
                it = providers_->erase(it);
                break;
            }
        }
    }

    if(mutexData_){
        vSemaphoreDelete(mutexData_);
        mutexData_ = nullptr;
    }
}

void StatusProvider::addData(StatusData* data)
{
    if(data != nullptr){
        if(firstData_ == nullptr){
            firstData_ = data;
        }else{
            StatusData* end = firstData_;
            while(end->getNext() != nullptr){
                end = end->getNext();
            }
            end->setNext(data);
        }
    }
}

void StatusProvider::insertStatusInJSON(JsonDocument& doc) const
{
    if(enabled_){
        JsonObject d = doc[name_].to<JsonObject>();
        StatusData* data = firstData_;
        while(data){
            if(data->isEnabled()){
                data->insertInJSON(d);
            }
            data = data->next_;
        }
    }
}

DataLock StatusProvider::lock() const
{
    return DataLock{mutexData_};
}

void StatusProvider::setProviderChanged()
{
    if(providersMutex_){
        DataLock lock(providersMutex_);
        providerChanged_ = true;
    }
}

bool StatusProvider::isDisplayable() const
{
    if(isEnabled()){ //Don't show it on screen if not enabled
        const StatusData* data = getFirstData();
        while(data){
            if(data->isDisplayableOnScreen()){
                return true;
            }
            data = data->getNext();
        }
    }
    return false;
}

const StatusProvider* StatusProvider::getNextEnabledProvider(const StatusProvider* actual, bool moveBegin)
{
    if(providersMutex_ && providers_){
        DataLock lock(providersMutex_);
        StatusProviders::iterator it=providers_->begin();
        if(actual){
            for(it=providers_->begin(); it != providers_->end(); ++it){
                if(*it == actual){
                    ++it;
                    break;
                }
            }
        }
        for(; it != providers_->end(); ++it){
            if((*it)->isEnabled()){
                break;
            }
        }
        if(moveBegin && (it == providers_->end())){
            it = providers_->begin();;
        }
        return (it == providers_->end()) ? nullptr : *it;
    }
    return nullptr;
}

StatusProvider* StatusProvider::getNextDisplayableProvider(const StatusProvider* actual, bool moveBegin)
{
    if(providersMutex_ && providers_){
        DataLock lock(providersMutex_);
        StatusProviders::iterator it=providers_->begin();
        if(actual){
            for(it=providers_->begin(); it != providers_->end(); ++it){
                if(*it == actual){
                    ++it;
                    break;
                }
            }
        }
        for(; it != providers_->end(); ++it){
            if((*it)->isDisplayable()){
                break;
            }
        }
        if(moveBegin && (it == providers_->end())){
            it = providers_->begin();
        }
        return (it == providers_->end()) ? nullptr : *it;
    }
    return nullptr;
}

StatusProvider* StatusProvider::getPreviousDisplayableProvider(const StatusProvider* actual, bool moveEnd)
{
    if(providersMutex_ && providers_){
        DataLock lock(providersMutex_);
        StatusProviders::reverse_iterator it=providers_->rbegin();
        if(actual){
            for(it=providers_->rbegin(); it != providers_->rend(); ++it){
                if(*it == actual){
                    ++it;
                    break;
                }
            }
        }
        for(; it != providers_->rend(); ++it){
            if((*it)->isDisplayable()){
                break;
            }
        }
        if(moveEnd && (it == providers_->rend())){
            it = providers_->rbegin();
        }
        return (it == providers_->rend()) ? nullptr : *it;
    }
    return nullptr;
}

bool StatusProvider::exists(const StatusProvider* provider)
{
    if(provider){
        DataLock lock(providersMutex_);
        for(StatusProviders::iterator it=providers_->begin(); it != providers_->end(); ++it){
            if(*it == provider){
                return true;
            }
        }
    }
    return false;
}


const StatusData* StatusProvider::locateSNMPData(const char* oid)
{
    const StatusProvider* provider = nullptr;
#ifdef OID_ALIAS
        for(int i=0;;++i){
            if(oidAlias[i][0] == nullptr){
                //End of our list
                break;
            }else{
                if(strcmp(oid, oidAlias[i][0]) == 0){
                    oid = oidAlias[i][1];   //Replace old oid by new one
                    break;
                }
            }
        }
#endif

    //Go trough all providers
    while(true){
        provider = StatusProvider::getNextEnabledProvider(provider, false);
        if(provider){
            //Go trough all data
            const StatusData* data = provider->getFirstData();
            while(data){
                if(data->isEnabled()){
                    //Data is enabled, check if it's an SNMP enabled data
                    const char* dataOID = data->getSNMPOID();
                    if(dataOID && (strcmp(dataOID, oid) == 0)){
                        //Match
                        return data;
                    }
                }
                data = data->getNext();
            }
        }else{
            break;
        }
    }
    return nullptr;
}

const StatusData* StatusProvider::locateNextSNMPData(const char* oid, bool& found, bool onlyEnabled)
{
    found = false;
    if(providersMutex_){
        DataLock lock(providersMutex_);
        const SNMPDataMap* map = listOID();
        for(SNMPDataMap::const_iterator it=map->begin(); it != map->end(); ++it){
            const StatusData* ret = nullptr;
            if(found){
                ret = it->second;
            }else if(it->first.rfind(oid, 0) == 0){
                //OID starts with something we know, next data should be ok
                found = true;
                if(it->first != oid){
                    //Not a perfect match, so we have already the next data here :)
                    ret = it->second;
                }
            }

            if(ret){
                if(onlyEnabled){
                    if(ret->isEffectivelyEnabled()){
                        //Return, if not enabled, move to next
                        return ret;
                    }
                }else{
                    return ret;
                }
            }
        }
    }
    return nullptr;
}

void StatusProvider::listOIDInJSON(JsonDocument& doc)
{
    if(providersMutex_){
        JsonArray data = doc["data"].to<JsonArray>();
        DataLock lock(providersMutex_);
        const StatusProvider::SNMPDataMap* map = StatusProvider::listOID();
        for(StatusProvider::SNMPDataMap::const_iterator it=map->begin(); it != map->end(); ++it){
            JsonObject snmpEntry = data.add<JsonObject>();
            snmpEntry["OID"] = it->first;
            snmpEntry["name"] = it->second->getName();
            snmpEntry["value"] = it->second->toString();
            bool active = it->second->isEnabled();
            snmpEntry["active"] = it->second->isEffectivelyEnabled();
            const StatusProvider* prov = it->second->getProvider();
            if(prov){
                snmpEntry["provider"] = prov->getName();
            }
        }
    }
}

const StatusProvider::SNMPDataMap* StatusProvider::listOID()
{
    SNMPDataMap* ret = nullptr;
    if(!snmpData_ || providerChanged_){
        providerChanged_ = false;
        if(snmpData_){
            delete snmpData_;
            snmpData_ = nullptr;
        }
        snmpData_ = new SNMPDataMap();
        for(StatusProviders::const_iterator it = providers_->begin(); it != providers_->end(); ++it){
            const StatusData* data = (*it)->getFirstData();
                while(data){
                    //Data is enabled, check if it's an SNMP enabled data
                    const char* dataOID = data->getSNMPOID();
                    if(dataOID){
                        //Insert into the map
                        (*snmpData_)[dataOID] = data;
                    }
                    data = data->getNext();
                }
        }
    }
    ret = snmpData_;
    return ret;
}


/**
 * Used to compare to OID in map
 */
bool StatusProvider::OIDComparator::operator()(const std::string& oid1, const std::string& oid2) const
{
    std::string::size_type startPos1 = 0;
    std::string::size_type startPos2 = 0;
    while(true){
        std::string::size_type pos1 = oid1.find_first_of('.', startPos1);
        std::string::size_type pos2 = oid2.find_first_of('.', startPos2);
        if(pos1 == std::string::npos){
            pos1 = oid1.length();
        }
        if(pos2 == std::string::npos){
            pos2 = oid2.length();
        }

    std::string sub1 = oid1.substr(startPos1, pos1-startPos1);
    std::string sub2 = oid2.substr(startPos2, pos2-startPos2);
    int digit1 = std::atoi(sub1.c_str());
    int digit2 = std::atoi(sub2.c_str());
    if(digit1 < digit2){
        return true;
    }else if(digit1 > digit2){
        return false;
    }
        if(pos1 == oid1.length()){
            if(pos2 != oid2.length()){
                return true;
            }
            return false;
        }
        if(pos2 == oid2.length()){
            if(pos1 != oid1.length()){
                return false;
            }
            return true;
        }
        startPos1 = pos1 + 1;
        startPos2 = pos2 + 1;
    }
}

void StatusProvider::getActiveAlarms(DataCollection& col)
{
    col.clear();
    DataLock lock(providersMutex_);
    for(StatusProviders::const_iterator it = providers_->begin(); it != providers_->end(); ++it){
        const StatusData* data = (*it)->getFirstData();
        while(data){
            if(data->isEnabled() && data->isAlarmActive()){
                col.insert(std::pair{data->getLastUpdate(), data});
            }
            data = data->getNext();
        }
    }
}

AlarmSeverity StatusProvider::getMostAlarmSeverity()
{
    AlarmSeverity ret = AlarmSeverity::INACTIVE;
    DataLock lock(providersMutex_);
    for(StatusProviders::const_iterator it = providers_->begin(); it != providers_->end(); ++it){
        const StatusData* data = (*it)->getFirstData();
        while(data){
            if(data->isEnabled() && (data->getAlarmSeverity() > ret)){
                ret = data->getAlarmSeverity();
                if(ret == AlarmSeverity::ERROR){
                    break;
                }
            }
            data = data->getNext();
        }
    }
    return ret;
}

void StatusProvider::getMostCriticalAlarm(AlarmInfo& info)
{
    info.severity = AlarmSeverity::INACTIVE;
    DataLock lock(providersMutex_);
    for(StatusProviders::const_iterator it = providers_->begin(); it != providers_->end(); ++it){
        const StatusData* data = (*it)->getFirstData();
        while(data){
            if(data->isEnabled()){
                if(data->getAlarmSeverity() > info.severity){
                    //Severity is better with the new alarm
                    data->getAlarm(info);
                }else if((data->getAlarmSeverity() == info.severity) && (data->getAlarmTime() > data->getAlarmTime())){
                    //Alarm is more recent
                    data->getAlarm(info);
                }
            }
            data = data->getNext();
        }
    }
}

void StatusProvider::registerAlarmListener(AlarmListener* cb)
{
    DataLock lock(providersMutex_);
    if(!alarmCallbacks_){
        alarmCallbacks_ = new AlarmCallbacks();
    }
    alarmCallbacks_->push_back(cb);
}


void StatusProvider::unregisterAlarmListener(AlarmListener* cb)
{
    DataLock lock(providersMutex_);
    if(alarmCallbacks_){
        for(AlarmCallbacks::iterator it=alarmCallbacks_->begin(); it != alarmCallbacks_->end(); ++it){
            if((*it) == cb){
                alarmCallbacks_->erase(it);
                break;
            }
        }
    }
}

void StatusProvider::alarmChanged(const StatusData* data)
{
    if(alarmCallbacks_){
        for(AlarmListener* cbFn : *alarmCallbacks_){
            cbFn->alarmChanged(data);
        }
    }
}

StatusData::StatusData(StatusProvider* provider, const char* name) : provider_{provider}, name_{name}, unit_{nullptr},
                                    snmpOID_{nullptr}, oledText_{nullptr}, lastUpdate_(0), enabled_{true}, showOnScreen_{true}, next_{nullptr},
                                    alarmInfo_{this}
{
    provider_->addData(this);
}

StatusData::StatusData(StatusProvider* provider, const char* name, const char* unit) : provider_{provider}, name_{name}, unit_{unit},
                            snmpOID_{nullptr}, oledText_{nullptr}, lastUpdate_{0}, enabled_{true}, showOnScreen_{true}, next_{nullptr},
                            alarmInfo_{this}
{
    provider_->addData(this);
}


int64_t StatusData::getLastUpdate() const
{
    DataLock l = lock();
    return lastUpdate_;
}

bool StatusData::isEffectivelyEnabled() const
{
    if(provider_){
        return isEnabled() && provider_->isEnabled();
    }
    return isEnabled();
}

void StatusData::setAlarm(AlarmSeverity severity)
{
    bool changed = false;
    if(severity != alarmInfo_.severity){
        changed = true;
    }
    alarmInfo_.severity = severity;
    if(changed){
        alarmInfo_.startTime = millis();
        StatusProvider::alarmChanged(this);
    }
}

void StatusData::clearAlarm()
{
    if(AlarmSeverity::INACTIVE != alarmInfo_.severity){
        alarmInfo_.severity = AlarmSeverity::INACTIVE;
        alarmInfo_.startTime = 0;
        StatusProvider::alarmChanged(this);
    }
}

bool StatusData::getAlarm(AlarmInfo& info) const
{
    DataLock l = lock();
    info = alarmInfo_;
    return alarmInfo_.severity != AlarmSeverity::INACTIVE;
}

AlarmSeverity StatusData::getAlarmSeverity() const
{
    DataLock l = lock();
    return alarmInfo_.severity;
}

template<>
std::string StatusValue<IPAddress>::toString() const
{
    return std::string(getValue().toString().c_str());
}

template<>
void StatusValue<IPAddress>::insertInJSON(JsonObject& object) const
{
    object[getName()] = getValue().toString();
}


template<>
std::string StatusValue<bool>::toString() const
{
    return getValue() ? "true" : "false";
}

template<>
std::string StatusValue<TimeTicks>::toString() const
{
    TimeTicks value = getValue();
    constexpr uint32_t hundredSToSeconds = 100;                     //1/100s to minutes
    constexpr uint32_t hundredSToMinutes = 60 * hundredSToSeconds;  //1/100s to minutes
    constexpr uint32_t hundredSToHours = 60 * hundredSToMinutes;    //1/100s to seconds
    constexpr uint32_t hundredSToDays = 24 * hundredSToHours;       //1/100s to days
    uint32_t days = value / hundredSToDays;
    value -= days * hundredSToDays;
    uint32_t hours = value / hundredSToHours;
    value -= hours * hundredSToHours;
    uint32_t minutes = value / hundredSToMinutes;
    value -= minutes * hundredSToMinutes;
    uint32_t seconds = value / hundredSToSeconds;
    value -= seconds * hundredSToSeconds;
    std::ostringstream oss;
    if(days > 0){
        oss << days << "d";
    }
    oss << hours << ":" << std::setfill('0') << std::setw(2) << minutes << std::setw(0) << ":" << std::setfill('0') << std::setw(2) << seconds << std::setw(0) << "." << std::setfill('0') << std::setw(2) << value;
    return oss.str();
}

template<>
void StatusValue<TimeTicks>::insertInJSON(JsonObject& object) const
{
    std::string nameStr = getName();
    nameStr += "_str";
    object[getName()] = getValue();
    object[nameStr] = toString();
}

/**
 * SNMP BER specialization
 * TODO: Can we use template to create BER?
 */
template<>
SNMP::BER* StatusValue<bool>::buildSNMPBER() const
{
    if(getSNMPOID()){
        return new SNMP::BooleanBER(getValue());
    }
    return nullptr;
}

template<>
SNMP::BER* StatusValue<int32_t>::buildSNMPBER() const
{
    if(getSNMPOID()){
        return new SNMP::IntegerBER(getValue());
    }
    return nullptr;
}

template<>
SNMP::BER* StatusValue<float>::buildSNMPBER() const
{
    if(getSNMPOID()){
        return new SNMP::IntegerBER(getValue() * 10);
    }
    return nullptr;
}

template<>
SNMP::BER* StatusValue<double>::buildSNMPBER() const
{
    if(getSNMPOID()){
        return new SNMP::FloatBER(getValue());
    }
    return nullptr;
}

template<>
SNMP::BER* StatusValue<IPAddress>::buildSNMPBER() const
{
    if(getSNMPOID()){
        return new SNMP::IPAddressBER(getValue());
    }
    return nullptr;
}

template<>
SNMP::BER* StatusValue<std::string>::buildSNMPBER() const
{
    if(getSNMPOID()){
        return new SNMP::OctetStringBER(getValue().c_str());
    }
    return nullptr;
}

template<>
SNMP::BER* StatusValue<TimeTicks>::buildSNMPBER() const
{
    if(getSNMPOID()){
        return new SNMP::TimeTicksBER(getValue());
    }
    return nullptr;
}

StatusEnumValue::StatusEnumValue(StatusProvider* provider, const char* name, std::initializer_list<EnumPair> vals) :
    StatusData{provider, name}, value_{0}/*, valsPairs_{vals}*/
{
    valsPairs_ = std::move(vals);
}

StatusEnumValue::StatusEnumValue(StatusProvider* provider, const char* name, std::initializer_list<EnumPair> vals, std::function<int()> getter) :
    StatusData{provider, name}, value_{0}/*, valsPairs_{vals}*/, getter_(getter)
{
    valsPairs_ = std::move(vals);
}

int StatusEnumValue::getValue() const
{
    if(getter_){
        return getter_();
    }
    DataLock l = StatusData::lock();
    return value_;
}

void StatusEnumValue::setValue(int value)
{
    DataLock l = StatusData::lock();
    value_ = value;
    updated();
}

std::string StatusEnumValue::toString() const
{
    int value = getValue();
    for(const EnumPair& pair : valsPairs_){
        if(pair.second){
            if(pair.first == value){
                return pair.second;
            }
        }else{
            break;
        }
    }
    return "---";
}

void StatusEnumValue::insertInJSON(JsonObject& doc) const
{
    if(isEnabled()){
        doc[getName()] = toString();
    }
}

SNMP::BER* StatusEnumValue::buildSNMPBER() const
{
    if(getSNMPOID()){
        return new SNMP::OctetStringBER(toString().c_str());
    }
    return nullptr;
}