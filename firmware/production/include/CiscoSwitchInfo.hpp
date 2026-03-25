#ifndef _CISCO_SWITCH_INFO_HPP_
#define _CISCO_SWITCH_INFO_HPP_

#include <SNMP.h>
#include <map>
#include <string>
#include <atomic>
#include <FreeRTOS.h>
#include <functional>
#include <StatusProvider.hpp>

/**
 * A physical sensor as found in SNMP
 */
class CiscoSensor
{
friend class CiscoSwitchInterface;

public:
    CiscoSensor();
    virtual ~CiscoSensor();
    /**
     * Sensor data type
     */
    enum class DataType {
        NONE,
        OTHER = 1,
        UNKNOWN,
        VOLTSAC,
        VOLTSDC,
        AMPERES,
        WATTS,
        HERTZ,
        CELSIUS,
        PERCENTRH,
        RPM,
        CMM,
        TRUTHVALUE,
        SPECIALENUM,
        DBM
    };

    enum class DataScale {
        NONE = 0,
        YOCTO,
        ZEPTO,
        ATTO,
        FEMTO,
        PICO,
        NANO,
        MICRO,
        MILLI,
        UNITS,
        KILO,
        MEGA,
        GIGA,
        TERA,
        EXA,
        PETA,
        ZETTA,
        YOTTA
    };

    /**
     * Sets sensor raw value
     * @param newValue Sensor integer value
     */
    void setValue(int newValue);

    /**
     * Sets the data scale
     * @param newScale
     */
    void setDataScale(DataScale newScale);

    /**
     * Sets the data type
     * @param newType New data type
     */
    void setDataType(DataType newType);

    /**
     * Gets physical index of the sensor
     * @return Physical index of the sensor
     */
    inline int getPhysicalIndex() const  { return physicalIndex; }

    /**
     * Sets physical index
     * @param index Physical index of the sensor
     */
    inline void setPhysicalIndex(int index) { physicalIndex = index; }

    /**
     * Gets sensor name
     */
    inline const std::string* getName() const { return &name; }

    /**
     * Sets sensor name
     */
    inline void setName(const char* name) { this->name = name; }

    /**
     * Gets data unit
     */
    inline const std::string* getUnit() const { return &unit; }

    /**
     * Compute unit string
     */
    void computeUnit();

    /**
     * Gets data precision
     */
    inline int getPrecision() const { return precision; }

    /**
     * Sets data precision
     */
    inline void setPrecision(int precision) { this->precision = precision; }

    /**
     * Gets value
     */
    inline double getValue() const { return value; }

    /**
     * Installs status provider
     */
    void installStatusProvider(StatusProvider* provider);

    /**
     * Destroy the status provider
     */
    void destroyStatusProvider();
private:
    DataType type;
    DataScale scale;
    int physicalIndex;
    std::string name;
    std::string unit;
    int precision;
    double value;
    StatusValue<double>* statusValue;
};

/**
 * Represent a physical interface
 *
 */
class CiscoSwitchInterface
{
public:
    CiscoSwitchInterface(int index);
    CiscoSwitchInterface(int index, bool isTrunk);

    inline int getInterfaceIndex() const { return interfaceIndex;  };
    inline int getPhysicalIndex() const { return physicalIndex; };
    inline bool isTrunk() const { return trunk; };
    inline CiscoSensor* getRXPowerSensor() { return &rxPowerSensor; };
    inline const std::string* getName() const { return &name; };
    inline const std::string* getCDPNeighbour() const { return &neighbour; };
    inline const std::string* getAlias() const { return &alias; }

    inline void setPhysicalIndex(int index) { physicalIndex = index; };
    void setName(const char* name);
    void setCDPNeighbour(const char* neighbour);
    void setAlias(const char* alias);

    void installStatusProvider();   //!< Install the status provider for this interface
    void destroyStatusProvider();   //!< Removes the status provider for this interface

    inline bool isOnScreen() { return statusProvider ?  statusProvider->isOnScreen() : false; };

private:
    int interfaceIndex;             //!< Index of the interface (10128, for example)
    int physicalIndex;              //!< Physical index (alias) of this interface
    bool trunk;                     //!< True if it's a trunk interface
    std::string name;               //!< Physical name of the interface (GigabitEthernet1/0/28, for example)
    std::string neighbour;          //!< CDP neighbour
    std::string alias;              //!< Interface alias
    CiscoSensor rxPowerSensor;      //!< RX power sensor
    StatusProvider* statusProvider;
    StatusValue<std::string>* cdpNeighbour;
    StatusValue<std::string>* ifAlias;
    StatusEnumValue* mode;
};

/**
 * Class used for getting Cisco switch info using SNMP
 */
class CiscoSwitchInfoFetcher
{
public:
    /**
     * Constructor
     */
    CiscoSwitchInfoFetcher();

    /**
     *
     * Destructor
     */
    virtual ~CiscoSwitchInfoFetcher();

    /**
     * Starts the fetcher
     */
    void start();

    /**
     * Stops the manager
     */
    void stop();


    /**
     * Sets the SNMP community string
     * @param community SNMP community string
     */
    inline void setSNMPCommunity(const std::string& community){ community_ = community; };

    /**
     * Sets the switch IP address
     * @param ip Switch IP address
     */
    void setSwitchIPAddress(const IPAddress& ip);

    /**
     * Sets the SNMP response timeout
     * @param timeoutMs Response timeout in milliseconds
     */
    inline void setSNMPResponseTimeout(unsigned long timeoutMs) { timeout_ = timeoutMs; };

private:
    /**
     * Loop
     */
    void loop();

    /**
     * Refreshes port data
     * @param intf Interface to refresh data
     */
    void refreshInterfaceInfo(CiscoSwitchInterface* intf);

    /**
     * FreeRTOS task function
     */
    static void taskFunction(void* param);

    /**
     * Sends an SNMP message and waits for reply
     * @param msg SNMP message request
     * @return SNMP agent response or nullptr if timeout
     */
    const SNMP::Message* sendRequest(SNMP::Message* msg);

    /**
     * Sends get request and call function when reply received
     * @param oid OID to request
     * @param cb callback function when response is received
     * @return true if response properly received
     */
    bool sendGetRequest(const char* oid, std::function<void(const SNMP::BER*)> cb);

    /**
     * Sends a get request with an index key
     * @param oid OID to request
     * @param index Index key to append to OID
     * @param cb callback function when response is received
     * @return true if response properly received
     */
    bool sendGetRequest(const char* oid, int index, std::function<void(const SNMP::BER*)> cb);

    /**
     * Send GetNext request to walk over a table
     * @param oid OID of the table
     * @param cd Callback to be called on each table row
     * @return True on success
     */
    bool sendGetSubtree(const char* oid, std::function<void(const std::string& oid, const SNMP::BER*)> cb);

    /**
     * Send GetNext request to walk over a table
     * @param oid OID of the table
     * @param index Index key to append to OID
     * @param cd Callback to be called on each table row
     * @return True on success
     */
    bool sendGetSubtree(const char* oid, int index, std::function<void(const std::string& oid, const SNMP::BER*)> cb);


    std::atomic<bool> started_;                                     //!< Are we started?
    unsigned long timeout_;                                         //!< Response timeout
    typedef std::map<int, CiscoSwitchInterface> InterfacesMap;      //!< Map type def
    InterfacesMap interfaces_;                                      //!< Interesting interfaces
    IPAddress switchAddr_;                                          //!< Switch IP address
    std::string community_;                                         //!< SNMP v2c community string
    TaskHandle_t taskHandle_;                                       //!< FreeRTOS task handle
    int32_t snmpFailures_;                                          //!< SNMP timeout count
    bool dirty_;                                                    //!< The interface map is dirty
    uint8_t buffer_[1400];                                          //!< Buffer for data
    /**
     * Gets all trunk enabled interfaces on the device
     * It will fill the trunkInterfaces_ map (with interface index as key)
     */
    void getTrunkInterfaces();

    /**
     * Gets all SFP physical indexes of type cevSensorTransceiverRxPwr
     * @param entries List of entries
     * @return false if failed (no response from agent)
     */
    bool getSensorsTransceiverRxPwr(std::vector<int>* entries);

    /**
     * Gets physical entity parent container
     * @param entity Entity to look for
     * @param container Container entity
     * @return false on timeout
     */
    bool getPhysicalContainedIn(int entity, int* container);

    /**
     * Gets SFTP interfaces for monitoring RX power
     * If the interface already exists (i.e. it's a trunk), it will be updated
     * if not, the interface will be inserted into the map
     */
    void getRXMeasurableInterfaces();

    /**
     * Gets physical index aliases and fill the interface (if found)
     */
    void getTrunkPhysicalAliases();

    /**
     * Gets trunk names for all found interfaces in the trunkInterfaces_ map
     */
    void getInterfacesNames();

    /**
     * Helper to get an OID index from the oid
     */
    static void getOIDIndex(const std::string& oid, std::string& index);

    /**
     * Gets interfaces CDP device id
     * @param interface Switch interface to get CDP neighbour
     */
    void getInterfaceCDP(CiscoSwitchInterface* interface);

    /**
     * Gets interface alias
     * @param interface Switch interface to get alias
     */
    void getInterfaceAlias(CiscoSwitchInterface* interface);

    /**
     * Enum for physical class
     */
    enum class PhysicalClass {
        NONE = 0,
        OTHER,
        UNKNOWN,
        CHASSIS,
        BACKPLANE,
        CONTAINER,
        POWER_SUPPLY,
        FAN,
        SENSOR,
        MODULE,
        PORT,
        STACK,
        CPU
    };

    /**
     * Gets physical class of a physical entity
     */
    PhysicalClass getPhysicalClass(int index);

    /**
     * Gets physical parent port
     * @param entity Child entity to look at
     * @param portEntity Parent port entity, -1 if not found
     * @return false in case of timeout
     */
    bool getPhysicalParentPort(int entity, int& portEntity);

    /**
     * Gets physical alias
     * @param entity Physical entity to get alias from
     * @param alias Physical entity alias (TODO: OID returns a OctetStringBER in place of int?)
     * @return false on timeout
     */
    bool getPhysicalAlias(int entity, int* alias);

    /**
     * Gets sensor value
     * @param entity Sensor physical entity
     * @param value Sensor value read
     * @return false if timeout
     */
    bool getSensorValue(CiscoSensor* sensor);

    /**
     * Gets sensor constants
     */
    void getSensorConstants(CiscoSensor* sensor);

    /**
     * Gets interface actually on OLED screen
     */
    CiscoSwitchInterface* getInterfaceOnScreen();

    static constexpr int MAX_FAILURES = 5;                                                      //Allow 5 SNMP failures in a row before going dirty
    static constexpr int TRUNKING = 1;                                                          //!< Interface is trunking
    static constexpr const char* entPhysicalAliasOID = "1.3.6.1.2.1.47.1.1.1.1.14";
    static constexpr const char* vlanTrunkPortDynamicStatus = "1.3.6.1.4.1.9.9.46.1.6.1.1.14";  //OID to get interfaces configured as trunk
    static constexpr const char* cevSensorTransceiverRxPwr = "1.3.6.1.4.1.9.12.3.1.8.46";       //Sensor transceiver RX power type
    static constexpr const char* entPhysicalVendorType = "1.3.6.1.2.1.47.1.1.1.1.3";            //Physical vendor type
    static constexpr const char* entPhysicalContainedIn = "1.3.6.1.2.1.47.1.1.1.1.4";
    static constexpr const char* entPhysicalName = "1.3.6.1.2.1.47.1.1.1.1.7";                  //The textual name of the physical entity
    static constexpr const char* cdpCacheDeviceId = "1.3.6.1.4.1.9.9.23.1.2.1.1.6";             //The Device-ID string as reported in the most recent CDP message.
    static constexpr const char* entPhysicalClass = "1.3.6.1.2.1.47.1.1.1.1.5";                 //An indication of the general hardware type of the physical entity.
    static constexpr const char* entSensorValue = "1.3.6.1.4.1.9.9.91.1.1.1.1.4";               //This variable reports the most recent measurement seen by the sensor.
    static constexpr const char* entSensorScale = "1.3.6.1.4.1.9.9.91.1.1.1.1.2";               //This variable indicates the exponent to apply to sensor values reported by entSensorValue.
    static constexpr const char* entSensorType = "1.3.6.1.4.1.9.9.91.1.1.1.1.1";                //This variable indicates the type of data reported by the entSensorValue.
    static constexpr const char* entSensorPrecision = "1.3.6.1.4.1.9.9.91.1.1.1.1.3";           //This variable indicates the number of decimal places of precision in fixed-point sensor values reported by entSensorValue.
    static constexpr const char* ifAlias = "1.3.6.1.2.1.31.1.1.1.18";                           //This object is an 'alias' name for the interface as specified by a network manager, and provides a non-volatile 'handle' for the interface.
};


extern CiscoSwitchInfoFetcher ciscoFetcher;
#endif