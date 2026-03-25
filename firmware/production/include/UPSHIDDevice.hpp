#ifndef __UPS_HID_DEVICE_HPP__
#define __UPS_HID_DEVICE_HPP__

#include <Arduino.h>
#include <OptionalData.hpp>
#include <FreeRTOS.h>
#include <string>
#include <ArduinoJson.h>
#include <StatusProvider.hpp>

#define DAEMON_TASK_LOOP_DELAY  3 // ticks
#define CLASS_TASK_LOOP_DELAY   3 // ticks
#define DAEMON_TASK_COREID      0
#define CLASS_TASK_COREID       0
#include <usb_host_hid_bridge.h>

void config_desc_cb(const usb_config_desc_t *config_desc);
void device_info_cb(usb_device_info_t *dev_info);
void hid_report_descriptor_cb(usb_transfer_t *transfer);
void hid_report_cb(usb_transfer_t *transfer);
void device_removed_cb();


class HIDData : public StatusData
{
public:
    HIDData(StatusProvider* provider, uint8_t usagePage, uint8_t usage, const char* name, const char* oledText = nullptr, const char* snmpOID = nullptr, const char* unit = nullptr);
    virtual ~HIDData();

    /**
     * Tests if the data match Usage page and usage combination
     */
    bool match(uint8_t usagePage, uint8_t usage);

    /**
     * Sets if the data is used
     */
    void setUsed(bool used);

    /**
     * Gets if the data is used
     */
    bool isUsed() const;

    /**
     * Sets report Id associated with this data
     */
    inline void setReportId(uint8_t reportId){ reportId_ = reportId; };

    /**
     * Gets associated report ID
     */
    inline uint8_t getReportId() const { return reportId_; };

    /**
     * Sets bits configuration
     * @param place Bit 0 place in the report data
     * @param count number of bits representing data
     */
    inline void setBitsConfiguration(uint8_t place, uint8_t count) { bitPlace_ = place; bitWidth_ = count; };

    /**
     * Gets bits configuration
     * @param place Bit 0 place in the report data
     * @param count number of bits representing data
     * TODO: Remove this and replace with a getValue
     */
    inline void getBitsConfiguration(uint8_t& place, uint8_t& count) { place = bitPlace_; count = bitWidth_; };

    /**
     * Gets name of the data
     */
    inline const char* getName() const { return name_; };

    /*
     * Sets value from the HID report buffer
     * @param buffer HID report buffer (without reportId)
     * @param len Size of the report buffer (without reportId)
     */
    void setValue(const uint8_t* buffer, size_t len);

    /**
     * Gets value of the data
     */
    double getValue() const;

    inline void setLogicalMinimum(const OptionalData<int32_t>& minimum){ logicalMinimum_ = minimum; };
    inline void setLogicalMaximum(const OptionalData<int32_t>& maximum){ logicalMaximum_ = maximum; };
    inline void setPhysicalMinimum(const OptionalData<int32_t>& minimum){ physicalMinimum_ = minimum; };
    inline void setPhysicalMaximum(const OptionalData<int32_t>& maximum){ physicalMaximum_ = maximum; };

    inline void setUnitExponent(const OptionalData<int32_t>& exponent){ unitExponent_ = exponent; };
    inline void setUnit(const OptionalData<uint32_t>& unit){ unit_ = unit; };

    void reset();

    /**
     * Gets number of bits representing this data
     */
    inline uint8_t getBitWidth() const { return bitWidth_; }

    /**
     * Gets if data is bool
     */
    bool isBool() const;

    /**
     * Inherited from StatusData
     */
    std::string toString() const override;

    /**
     * Inherited from StatusData
     */
    void insertInJSON(JsonObject& doc) const override;

    /**
     * Inherited from StatusData
     */
    virtual SNMP::BER* buildSNMPBER() const override;
private:
    uint8_t usagePage_;
    uint8_t usage_;
    uint8_t reportId_;
    OptionalData<int32_t> logicalMinimum_;
    OptionalData<int32_t> logicalMaximum_;
    OptionalData<int32_t> physicalMinimum_;
    OptionalData<int32_t> physicalMaximum_;
    OptionalData<int32_t> unitExponent_;
    OptionalData<uint32_t> unit_;
    uint32_t bitPlace_;
    uint32_t bitWidth_;
    const char* name_;
    bool used_;
    double value_;
    SemaphoreHandle_t mutexData_;
};

class UPSHIDDevice : public StatusProvider
{
public:
    UPSHIDDevice();
    virtual ~UPSHIDDevice() = default;

    /**
     * Begin
     */
    void begin();

    /**
     * Parse an HID report
     * @param data Pointer to HID report data
     * @param Lenght of the data pointer
     */
    void buildFromHIDReport(const uint8_t* data, size_t dataLen);

    /**
     * USB interrupt HID data payload callback
     */
    void hidReportData(const uint8_t* data, size_t len);

    /**
     * USB device removed
     */
    void deviceRemoved();

    /**
     * Gets remaining battery capacity
     */
    const HIDData& getRemainingCapacity() const;

    /**
     * Gets if AC is present
     */
    const HIDData& getACPresent() const;

    /**
     * Gets if battery is charging
     */
    const HIDData& getCharging() const;

    /**
     * Gets if battery is discharging
     */
    const HIDData& getDischarging() const;

    /**
     * Gets if battery is present
     */
    const HIDData& getBatteryPresent() const;

    /**
     * Gets if battery needs replacement
     */
    const HIDData& getNeedReplacement() const;

    /**
     * Gets estimated runtime in seconds
     */
    const HIDData& getRuntimeToEmpty() const;

    /**
     * Gets if the UPS is connected
     */
    inline bool isConnected() const { return connected_.getValue(); }

    /**
     * Sets USB device information
     */
    void setDeviceInfo(usb_device_info_t *dev_info);

    /**
     * Gets USB manufacturer
     */
    inline const char* getManufacturer() const { return manufacturer_.getValue().c_str(); };

    /**
     * Gets USB model
     */
    inline const char* getModel() const { return model_.getValue().c_str(); };

    /**
     * Gets USB serial number
     */
    inline const char* getSerial() const { return serial_.getValue().c_str(); };

    /**
     * Gets status in JSON format
     */
    void statusToJSON(JsonDocument& doc) const;

    /**
     * Gets status in JSON format
     */
    void statusToJSONString(std::string& str) const;


private:

    /**
     * For storing HID global items
     */
    struct HIDGlobalItems{
        OptionalData<uint32_t> usagePage;         //Current usage page
        OptionalData<int32_t> logicalMinimum;     //Logical minimum value
        OptionalData<int32_t> logicalMaximum;     //Logical maximum value
        OptionalData<int32_t> physicalMinimum;    //Physical minimum value
        OptionalData<int32_t> physicalMaximum;    //Physical maximum value
        OptionalData<int32_t> unitExponent;       //Unit exponent
        OptionalData<uint32_t> unit;              //Unit values
        OptionalData<uint32_t> reportSize;        //Report size in bits
        OptionalData<uint8_t> reportID;           //Report ID
        OptionalData<uint32_t> reportCount;       //Report count (number of data fields for the item)
    };

    /**
     * Local item
     */
    struct HIDLocalItem {
        OptionalData<uint32_t> usage;
        OptionalData<uint32_t> usageMinimum;
        OptionalData<uint32_t> usageMaximum;
        OptionalData<uint32_t> designatorIndex;
        OptionalData<uint32_t> designatorMinimum;
        OptionalData<uint32_t> designatorMaximum;
        OptionalData<uint32_t> stringIndex;
        OptionalData<uint32_t> stringMinimum;
        OptionalData<uint32_t> stringMaximum;
        OptionalData<uint8_t> delimiter;

        inline void reset() {
            usage.reset();
            usageMinimum.reset();
            usageMaximum.reset();
            designatorIndex.reset();
            designatorMinimum.reset();
            designatorMaximum.reset();
            stringIndex.reset();
            stringMinimum.reset();
            delimiter.reset();
        };
    };

    /**
     * Represents a prefix in HID report
     */
    struct HIDReportItemPrefix {
        uint8_t bSize;
        enum class BTYPE : uint8_t {Main = 0, Global, Local, Reserved};
        BTYPE bType;
        //6.2.2.4 of HID 1.11 spec
        enum class MainTag : uint8_t {Input = 0x8, Output = 0x9, Feature = 0xb, Collection = 0xa, EndCollection = 0xc};
        //6.2.2.7 Of HID 1.11 spec
        enum class GlobalTag : uint8_t {UsagePage = 0x0, LogicalMin, LogicalMax, PhysicalMin, PhysicalMax, UnitExp, Unit, ReportSize, ReportID, ReportCount, Push, Pop};
        //6.2.2.8 of HID 1.11 spec
        enum class LocalTag : uint8_t {Usage=0x0, UsageMin, UsageMax, DesignatorIndex, DesignatorMin, DesignatorMax, StringIndex, StringMin, StringMax, Delimiter};
        union {
            MainTag mainTag;
            GlobalTag globalTag;
            LocalTag localTag;
            uint8_t data;
        } bTag;

        uint8_t raw;
        HIDReportItemPrefix(uint8_t item){
            raw = item;
            bSize = item & 0x3;
            if(bSize == 3){
                bSize = 4;
            }
            bType = static_cast<BTYPE>((item >> 2) & 0x3);
            bTag.data = (item >> 4) & 0xF;
        }
    };

    /**
     * Collection item meaning (6.2.2.6 of HID 1.11 spec)
     */
    enum CollectionItem : uint8_t {Physical = 0x0, Application, Logical, Report, NamedArray, UsageSwitch, UsageModifier};

    /*Various HID constants taken from
    Universal Serial Bus
    Usage Tables
    for
    HID Power Devices
    **/
    static constexpr uint8_t BATTERY_SYSTEM_PAGE = 0x85;

    static constexpr uint8_t REMAINING_CAPACITY_USAGE = 0x66;
    static constexpr uint8_t AC_PRESENT_USAGE = 0xd0;
    static constexpr uint8_t CHARGING_USAGE = 0x44;
    static constexpr uint8_t DISCHARGING_USAGE = 0x45;
    static constexpr uint8_t BATTERY_PRESENT_USAGE = 0xd1;
    static constexpr uint8_t NEEDS_REPLACEMENT_USAGE = 0x4b;
    static constexpr uint8_t RUN_TIME_TO_EMPTY_USAGE = 0x68;
    static constexpr uint8_t INTEREST_USAGES_COUNT = 7;

    StatusEnumValue connected_;
    StatusValue<std::string> manufacturer_;
    StatusValue<std::string> model_;
    StatusValue<std::string> serial_;
    HIDData datas_[INTEREST_USAGES_COUNT];

    static void printHIDReportItemPrefix(const HIDReportItemPrefix& item);
    static const char* mainTagToString(HIDReportItemPrefix::MainTag mainTag);
    static const char* globalTagToString(HIDReportItemPrefix::GlobalTag globalTag);
    static const char* localTagToString(HIDReportItemPrefix::LocalTag localTag);

    /**
     * Convert following bytes in the buffer to an unsigned int32_t
     */
    static int32_t toSignedInteger(const uint8_t* data, size_t len);

    static uint32_t toUnSignedInteger(const uint8_t* data, size_t len);

    /**
     * Updates the GlobalItem store
     */
    static void updateGlobalItems(HIDGlobalItems& store, const HIDReportItemPrefix& prefix, const uint8_t* data);

    /**
     * Updates the LocalItem store
     */
    static void updateLocalItems(HIDLocalItem& store, const HIDReportItemPrefix& prefix, const uint8_t* data);

    /**
     * Gets a string descriptor
     * @param str_desc ESP IDF string descriptor
     * @param dest Destination string
     */
    static void getStringDescriptor(const usb_str_desc_t *str_desc, std::string& dest);

    /**
     * Adds to JSON
     */
    static void addToJSON(const HIDData& data, JsonDocument& doc);

    UsbHostHidBridge hidBridge;
};

extern UPSHIDDevice upsDevice;

#endif