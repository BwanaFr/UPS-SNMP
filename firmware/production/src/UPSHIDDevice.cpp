#include <UPSHIDDevice.hpp>
#include "esp_log.h"
#include <limits>
#include <ArduinoJson.h>
#include <sstream>

static const char *TAG = "UPSHID";

UPSHIDDevice upsDevice;

/**
 * Configuration descriptor callback
 */
void config_desc_cb(const usb_config_desc_t *config_desc) {
    // usb_print_config_descriptor(config_desc, NULL);
}

/**
 * Device info callback
 */
void device_info_cb(usb_device_info_t *dev_info) {
    upsDevice.setDeviceInfo(dev_info);
}

void hid_report_descriptor_cb(usb_transfer_t *transfer) {
    uint8_t *const data = (uint8_t *const)(transfer->data_buffer + USB_SETUP_PACKET_SIZE);
    size_t len = transfer->actual_num_bytes - USB_SETUP_PACKET_SIZE;
    upsDevice.buildFromHIDReport(data, len);
}

void hid_report_cb(usb_transfer_t *transfer) {
    //
    // check HID Report Descriptor for usage
    //
    uint8_t *data = (uint8_t *)(transfer->data_buffer);
    upsDevice.hidReportData(data, transfer->actual_num_bytes);
}

/**
 * Callback when USB device is removed
 */
void device_removed_cb() {
    upsDevice.deviceRemoved();
}

HIDData::HIDData(StatusProvider* provider, uint8_t usagePage, uint8_t usage, const char* name, const char* oledText, const char* snmpOID, const char* unit) :
    StatusData(provider, name, unit),
    usagePage_(usagePage), usage_(usage), reportId_(0),
    bitPlace_(0), bitWidth_(0), name_(name), used_(false), value_(0.0)

{
    if(snmpOID){
        setSNMPOID(snmpOID);
    }
    if(oledText){
        setOLEDText(oledText);
    }

    mutexData_ = xSemaphoreCreateMutex();
    if(mutexData_ == NULL){
        ESP_LOGE(TAG, "Unable to create data mutex");
    }
    setEnabled(false);
}

HIDData::~HIDData()
{
    if(mutexData_){
        vSemaphoreDelete(mutexData_);
        mutexData_ = nullptr;
    }
}

bool HIDData::match(uint8_t usagePage, uint8_t usage)
{
    return (usagePage_ == usagePage) && (usage == usage_);
}

void HIDData::setValue(const uint8_t* buffer, size_t len)
{
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        int32_t ret = 0;
        //TODO: Maybe we don't need to recompute this every time

        int32_t physicalMin = 0;
        int32_t physicalMax = 0;
        int32_t unitExponent = 0;
        if((!physicalMaximum_) || (!physicalMinimum_) || ((physicalMaximum_.getValue() == 0) && (physicalMinimum_.getValue() == 0))){
            physicalMin = logicalMinimum_.getValue();
            physicalMax = logicalMaximum_.getValue();
        }
        if(unitExponent_){
            unitExponent = unitExponent_.getValue();
        }
        double factor = ((double)physicalMax - (double)physicalMin) / ((double)logicalMaximum_.getValue() - (double)logicalMinimum_.getValue());

        //TODO: Handle signed/unsigned (page 38 of HID 1.11)
        uint8_t byteNumber = bitPlace_ / 8;
        uint8_t startBit = bitPlace_ - (byteNumber*8);
        uint32_t bitMask = (1<<bitWidth_)-1;

        for(int i=0;i<bitWidth_;++i){
            ret |= ((buffer[byteNumber] >> startBit) & 0x1) << i;
            ++startBit;
            if(startBit >= 8){
                startBit = 0;
                ++byteNumber;
            }
        }
        value_ = (double)((ret - logicalMinimum_.getValue()) * factor) + physicalMin;
        if(/*(usage_ == RUN_TIME_TO_EMPTY_USAGE) &&*/ unit_ && (unit_.getValue() & 0x1000)){
            //Unit is seconds, convert to minutes
            //This is a ugly (temporary?) hack
            value_ /= 60.0;
        }
        // ESP_LOGI(TAG, "%s [ID: 0x%02x] Byte number : %u, mask : 0x%0x, value: %f (unit: 0x%x)", name_, reportId_, byteNumber, bitMask, value_, unit_ ? unit_.getValue() : 0);
        xSemaphoreGive(mutexData_);
    }
    updated();
}

void HIDData::setUsed(bool used)
{
    setEnabled(used);
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        used_ = used;
        xSemaphoreGive(mutexData_);
    }
};

bool HIDData::isUsed() const
{
    bool ret = false;
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        ret = used_;
        xSemaphoreGive(mutexData_);
    }
    return ret;
}

double HIDData::getValue() const
{
    double ret;
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        ret = used_ ? value_ : 0.0;
        xSemaphoreGive(mutexData_);
    }
    return ret;
}

void HIDData::reset()
{
    if(xSemaphoreTake(mutexData_, portMAX_DELAY ) == pdTRUE)
    {
        used_ = false;
        reportId_ = 0;
        logicalMinimum_.reset();
        logicalMaximum_.reset();
        physicalMinimum_.reset();
        physicalMaximum_.reset();
        unitExponent_.reset();
        bitPlace_ = 0;
        bitWidth_ = 0;
        xSemaphoreGive(mutexData_);
    }
    setEnabled(false);
}

bool HIDData::isBool() const
{
    if(bitWidth_ == 1){
        return true;
    }
    //Assume a value between 1 and 0 to be boolean
    if(logicalMinimum_ && (logicalMinimum_.getValue() == 0) &&
        logicalMaximum_ && (logicalMaximum_.getValue() == 1)){
            return true;
    }

    return false;
}

std::string HIDData::toString() const
{
    std::ostringstream oss;
    if(isBool()){
        oss << (getValue() == 0 ? "false" : "true");
    }else if(unit_ && (unit_.getValue() & 0x1000)){
        //Unit of data is seconds, round to closest minute
        oss << round(value_);
    }else{
        oss << getValue();
    }
    return oss.str();
}

void HIDData::insertInJSON(JsonObject& doc) const
{
    if(isBool()){
        doc[getName()] = (getValue() == 0 ? false : true);
    }else{
        doc[getName()] = getValue();
    }
}

SNMP::BER* HIDData::buildSNMPBER() const
{
    if(getSNMPOID()){
        if(isBool()){
            return new SNMP::IntegerBER((getValue() == 0 ? 0 : 1));
        }else if(unit_ && (unit_.getValue() & 0x1000)){
            //Return integer for minutes
            return new SNMP::IntegerBER(round(value_));
        }else{
            return new SNMP::OpaqueBER(new SNMP::OpaqueFloatBER(getValue()));
        }
    }
    return nullptr;
}

UPSHIDDevice::UPSHIDDevice() :
    StatusProvider("UPS"),
    manufacturer_(this, "Brand"),
    connected_(this, "Status", {{0, "offline"}, {1, "online"}}),
    model_(this, "Model"), serial_(this, "Serial"),
    datas_{
        //List what can be interresting
        HIDData(this, BATTERY_SYSTEM_PAGE, REMAINING_CAPACITY_USAGE, "Remaining capacity", "Rem. capacity", "1.3.6.1.2.1.33.1.2.4", "%"),
        HIDData(this, BATTERY_SYSTEM_PAGE, RUN_TIME_TO_EMPTY_USAGE, "Run time to empty", "Rem. time", "1.3.6.1.2.1.33.1.2.3", "min"),
        HIDData(this, BATTERY_SYSTEM_PAGE, AC_PRESENT_USAGE, "AC present", nullptr, "1.3.6.1.2.1.33.1.6.1"),
        HIDData(this, BATTERY_SYSTEM_PAGE, CHARGING_USAGE, "Charging"),
        HIDData(this, BATTERY_SYSTEM_PAGE, DISCHARGING_USAGE, "Discharging"),
        HIDData(this, BATTERY_SYSTEM_PAGE, BATTERY_PRESENT_USAGE, "Battery present"),
        HIDData(this, BATTERY_SYSTEM_PAGE, NEEDS_REPLACEMENT_USAGE, "Needs replacement", "Replace btry")
    }
{
    manufacturer_.setEnabled(false);
    manufacturer_.setSNMPOID("1.3.6.1.2.1.33.1.1.1");
    model_.setEnabled(false);
    model_.setSNMPOID("1.3.6.1.2.1.33.1.1.2");
    serial_.setEnabled(false);
    //Raise an alarm in case UPS is not connected
    connected_.setAlarmText("UPS not connected");
    connected_.setAlarm(AlarmSeverity::WARNING);
}

void UPSHIDDevice::begin()
{
    hidBridge.onConfigDescriptorReceived = config_desc_cb;
    hidBridge.onDeviceInfoReceived = device_info_cb;
    hidBridge.onHidReportDescriptorReceived = hid_report_descriptor_cb;
    hidBridge.onReportReceived = hid_report_cb;
    hidBridge.onDeviceRemoved = device_removed_cb;
    hidBridge.begin();
}

void UPSHIDDevice::buildFromHIDReport(const uint8_t* data, size_t dataLen)
{
    HIDGlobalItems globalItems;
    HIDLocalItem localItems;
    uint32_t actualBit = 0;
    for(size_t i=0;i<dataLen;++i){
        HIDReportItemPrefix prefix(data[i]);
        updateGlobalItems(globalItems, prefix, &data[i+1]);
        updateLocalItems(localItems, prefix, &data[i+1]);
        if(prefix.bType == HIDReportItemPrefix::BTYPE::Global && prefix.bTag.globalTag == HIDReportItemPrefix::GlobalTag::ReportID){
            actualBit = 0;
        }else if(prefix.bType == HIDReportItemPrefix::BTYPE::Main){
            if(prefix.bTag.mainTag == HIDReportItemPrefix::MainTag::Input){
                for(int j=0;j<sizeof(datas_)/sizeof(HIDData);++j){
                    if(globalItems.usagePage && localItems.usage && datas_[j].match(globalItems.usagePage.getValue(), localItems.usage.getValue()) && !datas_[j].isUsed()){
                        datas_[j].setUsed(true);
                        datas_[j].setReportId(globalItems.reportID.getValue());
                        datas_[j].setBitsConfiguration(actualBit, globalItems.reportSize.getValue());
                        datas_[j].setLogicalMaximum(globalItems.logicalMaximum);
                        datas_[j].setLogicalMinimum(globalItems.logicalMinimum);
                        datas_[j].setPhysicalMaximum(globalItems.physicalMaximum);
                        datas_[j].setPhysicalMinimum(globalItems.physicalMinimum);
                        datas_[j].setUnitExponent(globalItems.unitExponent);
                        datas_[j].setUnit(globalItems.unit);
                        connected_.setValue(true);
                        connected_.clearAlarm();
                    }
                }
                actualBit += globalItems.reportCount * globalItems.reportSize;
            }
            localItems.reset();
        }
        //Advance in buffer
        i += prefix.bSize;
    }
}

void UPSHIDDevice::hidReportData(const uint8_t* data, size_t len)
{
    uint8_t reportID = data[0];
    //Go trough interresting data to check if the Id report match
    for(int j=0;j<sizeof(datas_)/sizeof(HIDData);++j){
        if(reportID == datas_[j].getReportId()){
            datas_[j].setValue(&data[1], len-1);
        }
    }
}

void UPSHIDDevice::deviceRemoved()
{
    ESP_LOGI(TAG, "Device removed");
    connected_.setValue(false);
    connected_.setAlarm(AlarmSeverity::WARNING);
    //Reset existing reports
    for(int j=0;j<sizeof(datas_)/sizeof(HIDData);++j){
        datas_[j].reset();
    }
    manufacturer_.setEnabled(false);
    model_.setEnabled(false);
    serial_.setEnabled(false);
}

const HIDData& UPSHIDDevice::getRemainingCapacity() const
{
    return datas_[0];
}

/**
 * Gets if AC is present
 */
const HIDData& UPSHIDDevice::getACPresent() const
{
    return datas_[1];
}

const HIDData& UPSHIDDevice::getCharging() const
{
    return datas_[2];
}

const HIDData& UPSHIDDevice::getDischarging() const
{
    return datas_[3];
}

const HIDData& UPSHIDDevice::getBatteryPresent() const
{
    return datas_[4];
}


const HIDData& UPSHIDDevice::getNeedReplacement() const
{
    return datas_[5];
}


const HIDData& UPSHIDDevice::getRuntimeToEmpty() const
{
    return datas_[6];
}

void UPSHIDDevice::updateGlobalItems(HIDGlobalItems& store, const HIDReportItemPrefix& prefix, const uint8_t* data)
{
    if(prefix.bType == HIDReportItemPrefix::BTYPE::Global){

        switch(prefix.bTag.globalTag){
            case HIDReportItemPrefix::GlobalTag::UsagePage:
                store.usagePage.setValue(toUnSignedInteger(data, prefix.bSize));
                break;
            case HIDReportItemPrefix::GlobalTag::LogicalMin:
                store.logicalMinimum.setValue(toSignedInteger(data, prefix.bSize));
                break;
            case HIDReportItemPrefix::GlobalTag::LogicalMax:
                store.logicalMaximum.setValue(toSignedInteger(data, prefix.bSize));
                break;
            case HIDReportItemPrefix::GlobalTag::PhysicalMin:
                store.physicalMinimum.setValue(toSignedInteger(data, prefix.bSize));
                break;
            case HIDReportItemPrefix::GlobalTag::PhysicalMax:
                store.physicalMaximum.setValue(toSignedInteger(data, prefix.bSize));
                break;
            case HIDReportItemPrefix::GlobalTag::UnitExp:
                store.unitExponent.setValue(toSignedInteger(data, prefix.bSize));
                break;
            case HIDReportItemPrefix::GlobalTag::Unit:
                store.unit.setValue(toUnSignedInteger(data, prefix.bSize));
                break;
            case HIDReportItemPrefix::GlobalTag::ReportSize:
                store.reportSize.setValue(toUnSignedInteger(data, prefix.bSize));
                break;
            case HIDReportItemPrefix::GlobalTag::ReportID:
                store.reportID.setValue(data[0]);
                break;
            case HIDReportItemPrefix::GlobalTag::ReportCount:
                store.reportCount.setValue(toUnSignedInteger(data, prefix.bSize));
                break;
        }
    }
}

void UPSHIDDevice::updateLocalItems(HIDLocalItem& store, const HIDReportItemPrefix& prefix, const uint8_t* data)
{
    if(prefix.bType == HIDReportItemPrefix::BTYPE::Local){
        switch(prefix.bTag.localTag){
            case HIDReportItemPrefix::LocalTag::Usage:
                store.usage.setValue(toUnSignedInteger(data, prefix.bSize));
                break;
            case HIDReportItemPrefix::LocalTag::UsageMin:
                store.usageMinimum.setValue(toUnSignedInteger(data, prefix.bSize));
                break;
            case HIDReportItemPrefix::LocalTag::UsageMax:
                store.usageMaximum.setValue(toUnSignedInteger(data, prefix.bSize));
                break;
            case HIDReportItemPrefix::LocalTag::DesignatorIndex:
                store.designatorIndex.setValue(toUnSignedInteger(data, prefix.bSize));
                break;
            case HIDReportItemPrefix::LocalTag::DesignatorMin:
                store.designatorMinimum.setValue(toUnSignedInteger(data, prefix.bSize));
                break;
            case HIDReportItemPrefix::LocalTag::DesignatorMax:
                store.designatorMaximum.setValue(toUnSignedInteger(data, prefix.bSize));
                break;
            case HIDReportItemPrefix::LocalTag::StringIndex:
                store.stringIndex.setValue(toUnSignedInteger(data, prefix.bSize));
                break;
            case HIDReportItemPrefix::LocalTag::StringMin:
                store.stringMinimum.setValue(toUnSignedInteger(data, prefix.bSize));
                break;
            case HIDReportItemPrefix::LocalTag::StringMax:
                store.stringMaximum.setValue(toUnSignedInteger(data, prefix.bSize));
                break;
            case HIDReportItemPrefix::LocalTag::Delimiter:
                store.delimiter.setValue(toUnSignedInteger(data, prefix.bSize));
                break;
        }
    }
}

void UPSHIDDevice::printHIDReportItemPrefix(const HIDReportItemPrefix& item)
{
    const char* bType = nullptr;
    const char* tagString = nullptr;
    switch(item.bType){
        case HIDReportItemPrefix::BTYPE::Global:
            bType = "Global";
            tagString = globalTagToString(item.bTag.globalTag);
            break;
        case HIDReportItemPrefix::BTYPE::Local:
            bType = "Local";
            tagString = localTagToString(item.bTag.localTag);
            break;
        case HIDReportItemPrefix::BTYPE::Main:
            bType = "Main";
            tagString = mainTagToString(item.bTag.mainTag);
            break;
        default:
            ESP_LOGI(TAG, "Unsupported bType");
            return;
    };
    if(tagString == nullptr){
        ESP_LOGI(TAG, "Invalid item : 0x%02x", item.raw);
    }else{
        ESP_LOGI(TAG, "Item(%s) : %s, data=%d bytes", bType, tagString, item.bSize);
    }
}

const char* UPSHIDDevice::mainTagToString(HIDReportItemPrefix::MainTag mainTag)
{
    switch (mainTag)
    {
    case HIDReportItemPrefix::MainTag::Input:
        return "Input";
    case HIDReportItemPrefix::MainTag::Output:
        return "Output";
    case HIDReportItemPrefix::MainTag::Feature:
        return "Feature";
    case HIDReportItemPrefix::MainTag::Collection:
        return "Collection";
    case HIDReportItemPrefix::MainTag::EndCollection:
        return "EndCollection";
    }
    return nullptr;
}

const char* UPSHIDDevice::globalTagToString(HIDReportItemPrefix::GlobalTag globalTag)
{
    switch(globalTag)
    {
    case HIDReportItemPrefix::GlobalTag::UsagePage:
        return "UsagePage";
    case HIDReportItemPrefix::GlobalTag::LogicalMin:
        return "Logical minimum";
    case HIDReportItemPrefix::GlobalTag::LogicalMax:
        return "Logical maximum";
    case HIDReportItemPrefix::GlobalTag::PhysicalMin:
        return "Physical minimum";
    case HIDReportItemPrefix::GlobalTag::PhysicalMax:
        return "Physical maximum";
    case HIDReportItemPrefix::GlobalTag::UnitExp:
        return "Unit exponent";
    case HIDReportItemPrefix::GlobalTag::Unit:
        return "Unit";
    case HIDReportItemPrefix::GlobalTag::ReportSize:
        return "Report size";
    case HIDReportItemPrefix::GlobalTag::ReportID:
        return "Report ID";
    case HIDReportItemPrefix::GlobalTag::ReportCount:
        return "Report count";
    case HIDReportItemPrefix::GlobalTag::Push:
        return "Push";
    case HIDReportItemPrefix::GlobalTag::Pop:
        return "Pop";
    }
    return nullptr;
}

const char* UPSHIDDevice::localTagToString(HIDReportItemPrefix::LocalTag localTag)
{
    switch(localTag){
    case HIDReportItemPrefix::LocalTag::Usage:
        return "Usage";
    case HIDReportItemPrefix::LocalTag::UsageMin:
        return "Usage minimum";
    case HIDReportItemPrefix::LocalTag::UsageMax:
        return "Usage maximum";
    case HIDReportItemPrefix::LocalTag::DesignatorIndex:
        return "Designator index";
    case HIDReportItemPrefix::LocalTag::DesignatorMin:
        return "Designator minimum";
    case HIDReportItemPrefix::LocalTag::DesignatorMax:
        return "Designator maximum";
    case HIDReportItemPrefix::LocalTag::StringIndex:
        return "String index";
    case HIDReportItemPrefix::LocalTag::StringMin:
        return "String minimum";
    case HIDReportItemPrefix::LocalTag::StringMax:
        return "String maximum";
    case HIDReportItemPrefix::LocalTag::Delimiter:
        return "Delimiter";
    }
    return nullptr;
}

int32_t UPSHIDDevice::toSignedInteger(const uint8_t* data, size_t len)
{
    int32_t ret = 0;
	int32_t shift = 24;
	for(int i=len-1;i>=0;--i){
		ret |= data[i] << shift;
		shift -= 8;
	}
    if(len != 4){
	    int32_t divider = ((1<<((4-len)*8))-1);
	    return ret/divider;
    }
    return ret;
}

uint32_t UPSHIDDevice::toUnSignedInteger(const uint8_t* data, size_t len)
{
    uint32_t ret = 0;
    int32_t shift = 0;
	for(int i=0;i<len;++i){
		ret |= data[i] << shift;
		shift += 8;
	}
    return ret;
}

void UPSHIDDevice::setDeviceInfo(usb_device_info_t *dev_info)
{
    std::string desc;
    getStringDescriptor(dev_info->str_desc_manufacturer, desc);
    manufacturer_.setValue(desc);
    manufacturer_.setEnabled(true);
    getStringDescriptor(dev_info->str_desc_product, desc);
    model_.setValue(desc);
    model_.setEnabled(true);
    getStringDescriptor(dev_info->str_desc_serial_num, desc);
    serial_.setValue(desc);
    serial_.setEnabled(true);
}

void UPSHIDDevice::getStringDescriptor(const usb_str_desc_t *str_desc, std::string& dest)
{
    dest = "";
    if(str_desc){
        for (int i = 0; i < str_desc->bLength / 2; i++) {
            /*
            USB String descriptors of UTF-16.
            Right now We just skip any character larger than 0xFF to stay in BMP Basic Latin and Latin-1 Supplement range.
            */
            if (str_desc->wData[i] > 0xFF) {
                continue;
            }
            dest += (char)str_desc->wData[i];
        }
    }
}

void UPSHIDDevice::statusToJSON(JsonDocument& doc) const
{
    if(isConnected()){
        doc["UPS"]["status"] = "connected";
        addToJSON(getRemainingCapacity(), doc);
        addToJSON(getACPresent(), doc);
        addToJSON(getCharging(), doc);
        addToJSON(getDischarging(), doc);
        addToJSON(getBatteryPresent(), doc);
        addToJSON(getNeedReplacement(), doc);
        addToJSON(getRuntimeToEmpty(), doc);
        doc["UPS"]["model"] = getModel();
        doc["UPS"]["serial"] = getSerial();
    }else{
        doc["UPS"]["status"] = "disconnected";
    }
}

void UPSHIDDevice::statusToJSONString(std::string& str) const
{
    JsonDocument doc;
    statusToJSON(doc);
    serializeJson(doc, str);
}

void UPSHIDDevice::addToJSON(const HIDData& data, JsonDocument& doc)
{
    if(data.isUsed()){
        if(data.isBool()){
            //Boolean value
            doc["UPS"][data.getName()] = data.getValue() == 0 ? false : true;
        }else{
            doc["UPS"][data.getName()] = data.getValue();
        }
    }
}