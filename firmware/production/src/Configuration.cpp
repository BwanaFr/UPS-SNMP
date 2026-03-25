#include <Configuration.hpp>
#include "esp_log.h"
#include <LittleFS.h>

#include <ETH.h>
#include "mbedtls/aes.h"
#include "mbedtls/cipher.h"

#include <DevicePins.hpp>

#define DEFAULT_DEVICE_NAME "UPS-SNMP"
#define DEFAULT_USE_DHCP false
#define DEFAULT_IP "10.10.11.200"
#define DEFAULT_SUBNET "255.255.254.0"
#define DEFAULT_GATEWAY "10.10.10.1"
#define DEFAULT_SNMP_PUBLIC_COM "public"
#define DEFAULT_SNMP_PRIVATE_COM "private"
#define DEFAULT_PING_GATEWAY true
#define DEFAULT_PING_INTERVAL 1000
#define DEFAULT_PING_TIMEOUT 100

#define FLASH_SAVE_DELAY 2000
#define CFG_FILENAME "/config.json"

#define USER_BTN_PRESS_TIME 15000

static const char* TAG = "Configuration";

DeviceConfiguration configuration;

DeviceConfiguration::DeviceConfiguration() :
        lastChange_(0), mutexData_(nullptr), mutexListeners_(nullptr),
        deviceName_(DEFAULT_DEVICE_NAME),
        userName_(""), password_(""), useDHCP_(DEFAULT_USE_DHCP),
        ip_(DEFAULT_IP), subnet_(DEFAULT_SUBNET), gateway_(DEFAULT_GATEWAY),
        snmpPublicCommmunity_(DEFAULT_SNMP_PUBLIC_COM),
        pingTestIP_(INADDR_NONE), pingTestGateway_(DEFAULT_PING_GATEWAY),
        pingTestInterval_(DEFAULT_PING_INTERVAL), pingTestTimeout_(DEFAULT_PING_TIMEOUT),
        switchIP_(INADDR_NONE), macAddress_(""),
        lastButton_(false), lastPress_(0),
        cfgReset_(false), ipCfgChanged_(false),
        configTask_(nullptr)
{
    mutexData_ = xSemaphoreCreateMutex();
    if(mutexData_ == NULL){
        ESP_LOGE(TAG, "Unable to create data mutex");
    }

    mutexListeners_ = xSemaphoreCreateMutex();
    if(mutexListeners_ == NULL){
        ESP_LOGE(TAG, "Unable to create listeners mutex");
    }
}

DeviceConfiguration::~DeviceConfiguration()
{
    if(mutexData_){
        vSemaphoreDelete(mutexData_);
        mutexData_ = nullptr;
    }
    if(mutexListeners_){
        vSemaphoreDelete(mutexListeners_);
        mutexListeners_ = nullptr;
    }
}

void DeviceConfiguration::begin()
{
    LittleFS.begin(true);
    if(configTask_ == NULL){
        xTaskCreatePinnedToCore(configTask, "configTask", 4096, (void*)this, tskIDLE_PRIORITY + 1, &configTask_, ESP_TASK_MAIN_CORE ? 0 : 1);
    }
}

void DeviceConfiguration::configTask(void* param)
{
    DeviceConfiguration* config = static_cast<DeviceConfiguration*>(param);
    vTaskSuspend(NULL);
    while(true){
        config->loop();
    }
}

void DeviceConfiguration::load()
{
    File configFile = LittleFS.open(CFG_FILENAME, "r");
    if(configFile){
        JsonDocument json;
        if(deserializeJson(json, configFile) == DeserializationError::Ok) {
            bool valid, changed, ipchanged;
            fromJSON(json, changed, valid, ipchanged, true);
            //Loads credentials outside fromJSON
            if(json["Username"]){
                setUserName(json["Username"], true);
            }
            if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
            {
                if(json["Password"]){
                    password_ = json["Password"].as<std::string>();
                }
                lastChange_ = 0;        //Don't write to flash
                ipCfgChanged_ = false;  //Don't send delayed IP event
                xSemaphoreGive(mutexData_);
            }
        }
        configFile.close();
    }else{
        ESP_LOGI(TAG, "Unable to load configuration file, using default");
        //Send default settings, no valid JSON file found (empty flash)
        setDeviceName(deviceName_, true);
        setMACAddress(macAddress_, true);
        setSNMPPublicCommunity(snmpPublicCommmunity_, true);
        setPingTestConfiguration(pingTestGateway_, pingTestIP_, pingTestInterval_, pingTestTimeout_, true);
        setSwitchIP(switchIP_, true);

        //Finaly, loads IP configuration
        setIPConfiguration(useDHCP_, ip_, subnet_, gateway_, false);
        notifyListeners(Parameter::IP_CONFIGURATION);
    }
}

void DeviceConfiguration::fromJSON(const JsonDocument& doc, bool& changed, bool& valid, bool& ipChanged, bool forceNotification)
{
    changed = false;
    valid = false;
    ipChanged = false;
    //Device name
    if(doc["deviceName"].is<std::string>()){
        changed |= setDeviceName(doc["deviceName"].as<std::string>(), forceNotification);
        valid = true;
    }

    //MAC address override
    if(doc["MACAddress"].is<std::string>()){
        std::string mac = doc["MACAddress"].as<std::string>();
        changed |= setMACAddress(mac, forceNotification);
        valid = true;
    }

    //SNMP community
    if(doc["snmpROCommunity"].is<std::string>()){
        changed |= setSNMPPublicCommunity(doc["snmpROCommunity"].as<std::string>(), forceNotification);
        valid = true;
    }

    //Ping configuration
    bool pingGateway = false;
    IPAddress pingIP;
    int pingInterval = 0;
    int pingTimeout = 0;
    bool pingCfgSet = false;
    getPingTestConfiguration(pingGateway, pingIP, pingInterval, pingTimeout);

    if(doc["pingTestGateway"].is<bool>()){
        pingGateway = doc["pingTestGateway"].as<bool>();
        pingCfgSet = true;
    }

    if(doc["pingTestIP"].is<std::string>()){
        std::string ipStr = doc["pingTestIP"].as<std::string>();
        if(!ipStr.empty()){
            pingIP.fromString(ipStr.c_str());
        }else{
            pingIP = INADDR_NONE;
        }
        pingCfgSet = true;
    }
    if(doc["pingTestInterval"].is<int>()){
        pingInterval = doc["pingTestInterval"].as<int>();
        pingCfgSet = true;
    }
    if(doc["pingTestTimeout"].is<int>()){
        pingTimeout = doc["pingTestTimeout"].as<int>();
        pingCfgSet = true;
    }
    valid |= pingCfgSet;
    if(pingCfgSet){
       changed |= setPingTestConfiguration(pingGateway, pingIP, pingInterval, pingTimeout, forceNotification);
    }

    //Switch monitoring IP address
    if(doc["switchIP"].is<std::string>()){
        IPAddress swIP;
        getSwitchIP(swIP);
        std::string swIPStr = doc["switchIP"].as<std::string>();
        if(swIPStr.empty()){
            swIP = INADDR_NONE;
        }else{
            swIP.fromString(swIPStr.c_str());
        }
        changed |= setSwitchIP(swIP, forceNotification);
        valid = true;
    }

    //Load IP configuration
    IPAddress ip;
    IPAddress subnet;
    IPAddress gateway;
    bool useDHCP;
    bool ipCfgSet = false;
    getIPConfiguration(useDHCP, ip, subnet, gateway);

    if(doc["useDHCP"].is<bool>()){
        useDHCP = doc["useDHCP"].as<bool>();
        ipCfgSet = true;
    }
    if(doc["staticIP"].is<std::string>()){
        std::string ipStr = doc["staticIP"].as<std::string>();
        ip.fromString(ipStr.c_str());
        ipCfgSet = true;
    }
    if(doc["staticMask"].is<std::string>()){
        std::string ipStr = doc["staticMask"].as<std::string>();
        subnet.fromString(ipStr.c_str());
        ipCfgSet = true;
    }
    if(doc["staticGateway"].is<std::string>()){
        std::string ipStr = doc["staticGateway"].as<std::string>();
        gateway.fromString(ipStr.c_str());
        ipCfgSet = true;
    }
    valid |= ipCfgSet;
    if(ipCfgSet){
        ipChanged |= setIPConfiguration(useDHCP, ip, subnet, gateway, forceNotification);
        changed |= ipChanged;
    }
}


bool DeviceConfiguration::setDeviceName(const std::string& name, bool forceNotification)
{
    bool changed = false;
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        if(deviceName_ != name)
        {
            deviceName_ = name;
            configurationChanged();
            changed = true;
        }
        xSemaphoreGive(mutexData_);
        if(changed || forceNotification){
            notifyListeners(Parameter::DEVICE_NAME);
        }
    }
    return changed;
}

void DeviceConfiguration::getDeviceName(std::string& name)
{
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        name = deviceName_;
        xSemaphoreGive(mutexData_);
    }
}

bool DeviceConfiguration::setIPConfiguration(bool useDHCP, const IPAddress& ip, const IPAddress& subnet, const IPAddress& gateway, bool forceNotification)
{
    bool changed = false;
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        if((useDHCP_ != useDHCP) || (ip_ != ip) || (subnet_ != subnet) || (gateway_ != gateway))
        {
            useDHCP_ = useDHCP;
            ip_ = ip;
            subnet_ = subnet;
            gateway_ = gateway;
            configurationChanged();
            changed = true;
        }
        if(changed || forceNotification){
            ipCfgChanged_ = true;
        }
        xSemaphoreGive(mutexData_);
    }
    return changed;
}

void DeviceConfiguration::getIPConfiguration(bool& useDHCP, IPAddress& ip, IPAddress& subnet, IPAddress& gateway)
{
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        useDHCP = useDHCP_;
        ip = ip_;
        subnet = subnet_;
        gateway = gateway_;
        xSemaphoreGive(mutexData_);
    }
}

void DeviceConfiguration::getMACAddress(std::string& mac)
{
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        mac = macAddress_;
        xSemaphoreGive(mutexData_);
    }
}

bool DeviceConfiguration::setMACAddress(const std::string& mac, bool forceNotification)
{
    bool changed = false;
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        if(mac != macAddress_){
            macAddress_ = mac;
            configurationChanged();
            changed = true;
        }
        xSemaphoreGive(mutexData_);
        if(changed || forceNotification){
            notifyListeners(Parameter::MAC_ADDRESS);
        }
    }
    return changed;
}

bool DeviceConfiguration::setUserName(const std::string& user, bool forceNotification)
{
    bool changed = false;
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        if(userName_ != user){
            userName_ = user;
            configurationChanged();
            changed = true;
        }
        xSemaphoreGive(mutexData_);
        if(changed || forceNotification){
            notifyListeners(Parameter::LOGIN_USER);
        }
    }
    return changed;
}

void DeviceConfiguration::getUserName(std::string& user)
{
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        user = userName_;
        xSemaphoreGive(mutexData_);
    }
}

bool DeviceConfiguration::setPassword(const std::string& password, bool forceNotification)
{
    bool changed = false;
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        if(password_ != password){
            password_ = password;
            configurationChanged();
            changed = true;
        }
        xSemaphoreGive(mutexData_);
        if(changed || forceNotification){
            notifyListeners(Parameter::LOGIN_PASS);
        }
    }
    return changed;
}

void DeviceConfiguration::getPassword(std::string& password)
{
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        password = password_;
        xSemaphoreGive(mutexData_);
    }
}

bool DeviceConfiguration::setSNMPPublicCommunity(const std::string& comString, bool forceNotification)
{
    bool changed = false;
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        if(snmpPublicCommmunity_ != comString){
            snmpPublicCommmunity_ = comString;
            configurationChanged();
            changed = true;
        }
        xSemaphoreGive(mutexData_);
        if(changed || forceNotification){
            notifyListeners(Parameter::SNMP_COMMUNITY);
        }
    }
    return changed;
}

void DeviceConfiguration::getSNMPPublicCommunity(std::string& comString)
{
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        comString = snmpPublicCommmunity_;
        xSemaphoreGive(mutexData_);
    }
}

bool DeviceConfiguration::setPingTestConfiguration(bool useGateway, const IPAddress& ip, int interval, int timeout, bool forceNotification)
{
    bool changed = false;
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        if((pingTestGateway_ != useGateway) || (pingTestIP_ != ip)
                || (pingTestInterval_ != interval) || (pingTestTimeout_ != timeout)){
            pingTestGateway_ = useGateway;
            pingTestIP_ = ip;
            pingTestInterval_ = interval;
            pingTestTimeout_ = timeout;
            configurationChanged();
            changed = true;
        }
        xSemaphoreGive(mutexData_);
        if(changed || forceNotification){
            notifyListeners(Parameter::PING_TEST_CONFIGURATION);
        }
    }
    return changed;
}

void DeviceConfiguration::getPingTestConfiguration(bool& useGateway, IPAddress& ip, int& interval, int& timeout)
{
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        useGateway = pingTestGateway_;
        ip = pingTestIP_;
        interval = pingTestInterval_;
        timeout = pingTestTimeout_;
        xSemaphoreGive(mutexData_);
    }
}

bool DeviceConfiguration::setSwitchIP(const IPAddress& address, bool forceNotification)
{
    bool changed = false;
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        if(switchIP_ != address){
            switchIP_ = address;
            configurationChanged();
            changed = true;
        }
        xSemaphoreGive(mutexData_);
        if(changed || forceNotification){
            notifyListeners(Parameter::SWITCH_IP);
        }
    }
    return changed;
}

void DeviceConfiguration::getSwitchIP(IPAddress& address)
{
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        address = switchIP_;
        xSemaphoreGive(mutexData_);
    }
}

/**
 * Loop to delay flash writing
 * TODO: Remove this and use a task (suspended until setting change)
 */
void DeviceConfiguration::loop()
{
    vTaskDelay(pdMS_TO_TICKS(FLASH_SAVE_DELAY));
    bool doWrite = false;
    bool ipChanged = false;
    if(xSemaphoreTake(mutexData_, 0) == pdTRUE)
    {
        if(lastChange_ != 0){
            //Something changed
            doWrite = true;
            ipChanged = ipCfgChanged_;
            ipCfgChanged_ = false;
            lastChange_ = 0;
        }
        xSemaphoreGive(mutexData_);
    }

    if(doWrite){
        write();
    }

    //Check if IP configuration changed
    if(ipChanged){
        notifyListeners(Parameter::IP_CONFIGURATION);
    }
    bool suspend = false;
    if(xSemaphoreTake(mutexData_, 0) == pdTRUE)
    {
        suspend = (lastChange_ == 0);
        xSemaphoreGive(mutexData_);
    }
    if(suspend){
        //Suspend this task
        vTaskSuspend(NULL);
    }
}

void DeviceConfiguration::configurationChanged()
{
    //Sets last change flag
    lastChange_ = millis();
    //Wakeup to flash write task
    vTaskResume(configTask_);
}

/**
 * Create a JSON version of the configuration
 */
void DeviceConfiguration::toJSONString(std::string& str)
{
    JsonDocument doc;
    toJSON(doc);
    serializeJson(doc, str);
}

void DeviceConfiguration::toJSON(JsonDocument& doc, bool includeLogin)
{
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        doc["deviceName"] = deviceName_;
        doc["useDHCP"] = useDHCP_;
        doc["staticIP"] = ip_.toString();
        doc["staticMask"] = subnet_.toString();
        doc["staticGateway"] = gateway_.toString();
        doc["MACAddress"] = macAddress_;
        doc["snmpROCommunity"] = snmpPublicCommmunity_;
        doc["pingTestGateway"] = pingTestGateway_;
        if(pingTestIP_ != INADDR_NONE){
            doc["pingTestIP"] = pingTestIP_.toString();
        }
        doc["pingTestInterval"] = pingTestInterval_;
        doc["pingTestTimeout"] = pingTestTimeout_;
        if(switchIP_ != INADDR_NONE){
            doc["switchIP"] = switchIP_.toString();
        }

        if(includeLogin){
            doc["Username"] = userName_;
            doc["Password"] = password_;
        }
        xSemaphoreGive(mutexData_);
    }
}

void DeviceConfiguration::registerListener(ParameterListener listener)
{
    if(xSemaphoreTake(mutexListeners_, portMAX_DELAY ) == pdTRUE)
    {
        listeners_.push_back(listener);
        xSemaphoreGive(mutexListeners_);
    }
}

void DeviceConfiguration::notifyListeners(Parameter changed)
{
    if(xSemaphoreTake(mutexListeners_, portMAX_DELAY ) == pdTRUE)
    {
        for(ParameterListener l: listeners_){
            l(changed);
        }
        xSemaphoreGive(mutexListeners_);
    }
}

void DeviceConfiguration::write()
{
    ESP_LOGI(TAG, "Saving configuration to flash");
    File cfgFile = LittleFS.open(CFG_FILENAME, "w");
    JsonDocument doc;
    toJSON(doc, true);
    serializeJson(doc, cfgFile);
    cfgFile.close();
}

void DeviceConfiguration::generateKeys(unsigned char iv[16], unsigned char key[128])
{
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    ETH.macAddress(mac);
    for(int i=0;i<16;++i){
        iv[i] = mac[i%6];
    }
    for(int i=0;i<128;++i){
        key[i] = mac[i%6] + (mac[0] << 3);
    }
}

std::string DeviceConfiguration::encrypt(const std::string& input)
{
    unsigned char* out;
    std::string inData = input;
    unsigned long cipherLen = inData.size() + 16 - (inData.size() % 16);
    inData.resize(cipherLen);
    out = (unsigned char*)malloc(cipherLen);
    if(!out){
        ESP_LOGE(TAG, "Unable to allocate crypto buffer!");
    }
    unsigned char iv[16];
    unsigned char key[128];
    generateKeys(iv, key);
	mbedtls_aes_context aes;
	mbedtls_aes_init(&aes);
	mbedtls_aes_setkey_enc(&aes, key, 128);
	mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, cipherLen, iv, (const unsigned char*)input.c_str(), out);
	mbedtls_aes_free(&aes);

    std::string ret;
    ret.resize(cipherLen*2);
    for(int i=0;i<cipherLen;++i){
        sprintf(&ret[i*2], "%02X", out[i]);
    }
    return ret;
}

std::string DeviceConfiguration::decrypt(const std::string& input)
{
    std::string ret;
    size_t dataSize = input.length()/2;
    ret.resize(dataSize);
    unsigned char* in;
    unsigned char iv[16];
    unsigned char key[128];
    generateKeys(iv, key);

    in = (unsigned char*)malloc(dataSize);
    if(!in){
        ESP_LOGE(TAG, "Unable to allocate crypto buffer! (%u bytes)", dataSize);
    }else{
        for(int i=0;i<dataSize;++i){
            char data[3] = {input[i*2], input[i*2+1], '\0'};
            sscanf(data, "%02X", &in[i]);
        }
    }
    mbedtls_aes_context aes;
	mbedtls_aes_init(&aes);
	mbedtls_aes_setkey_enc(&aes, key, 128);
	mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, dataSize, iv, in, (unsigned char*)&ret[0]);
	mbedtls_aes_free(&aes);
    return ret;
}

void DeviceConfiguration::resetToDefault()
{
    setDeviceName(DEFAULT_DEVICE_NAME);
    setIPConfiguration(DEFAULT_USE_DHCP, DEFAULT_IP, DEFAULT_SUBNET, DEFAULT_GATEWAY);
    setUserName("");
    setPassword("");
    setSNMPPublicCommunity(DEFAULT_SNMP_PUBLIC_COM);
    setPingTestConfiguration(DEFAULT_PING_GATEWAY, INADDR_NONE, DEFAULT_PING_INTERVAL, DEFAULT_PING_TIMEOUT);
    setSwitchIP(INADDR_NONE);
}