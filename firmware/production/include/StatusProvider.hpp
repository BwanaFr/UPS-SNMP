#ifndef _STATUS_PROVIDER__
#define _STATUS_PROVIDER__

#include <IPAddress.h>
#include <ArduinoJson.h>
#include <sstream>
#include <functional>
#include <FreeRTOS.h>
#include <initializer_list>
#include <deque>
#include <utility>
#include <map>

#include <SNMP.h>

class StatusData;

typedef uint64_t TimeTicks;

/**
 * A simple mutex/lock
 */
class DataLock
{
public:
    inline DataLock(SemaphoreHandle_t mutex) : mutex_(mutex)
    {
        lock();
    }

    inline DataLock(const DataLock& other) : mutex_(other.mutex_){}

    inline virtual ~DataLock(){
        unlock();
    }

    inline void unlock() {
        xSemaphoreGive(mutex_);
    }

    inline void lock() {
        while(xSemaphoreTake(mutex_, portMAX_DELAY ) != pdTRUE){};
    }

private:
    SemaphoreHandle_t mutex_;
};

/**
 * Alarm severity
 */
enum class AlarmSeverity{
    INACTIVE = 0,
    INFO,
    WARNING,
    ERROR
};

/**
 * Alarm information
 */
struct AlarmInfo
{
    AlarmSeverity severity;     //!< Alarm severity
    std::string text;           //!< Alarm text (dynamic)
    unsigned long startTime;    //!< Alarm start time (from millis() function)
    const StatusData* data;     //!< Associated StatusData

    AlarmInfo() : severity(AlarmSeverity::INACTIVE), startTime(0), data{nullptr}{}
    AlarmInfo(const StatusData* data) : severity(AlarmSeverity::INACTIVE), startTime(0), data{data}{}
};

class AlarmListener
{
public:
    virtual void alarmChanged(const AlarmInfo& info) = 0;
};

/**
 * This class is used to implement a provider for publishing status data
 */
class StatusProvider
{
friend class StatusData;
public:
    /**
     * Constructor
     * @param name Name of the provider (title on the OLED screen, parent object name on the JSON document)
     */
    StatusProvider(const char* name);

    /**
     * Default destructor
     */
    virtual ~StatusProvider();

    /**
     * Gets pointer to the first data provided by this
     * @return pointer to the first data
     */
    inline const StatusData* getFirstData() const { return firstData_; };

    /**
     * Gets name of this provider
     * @return Name of the provider
     */
    inline const char* getName() const { return name_; };

    /**
     * Inserts this provider in the specified JSON document
     * @param doc JSON document to insert this provider data
     */
    void insertStatusInJSON(JsonDocument& doc) const;

    /**
     * Gets if the provider is enabled
     * @return true if the provider is enabled
     */
    inline bool isEnabled() const  { return enabled_; };

    /**
     * Gets if this provider is displayabled on screen
     */
    bool isDisplayable() const;

    /**
     * Gets next enabled provider
     * @param actual Actual provider
     * @param moveBegin True if first item is retrieved at the end
     * @return Next provider or nullptr if at end
     */
    static const StatusProvider* getNextEnabledProvider(const StatusProvider* actual = nullptr, bool moveBegin = true);

    /**
     * Gets next provider with displayable status
     * @param actual Actual provider
     * @param moveBegin True if first item is retrieved at the end
     * @return Next provider or nullptr if at end
     */
    static StatusProvider* getNextDisplayableProvider(const StatusProvider* actual = nullptr, bool moveBegin = true);

    /**
     * @param actual Actual provider
     * @param moveEnd True if first item is retrieved at the start
     * @return Next provider or nullptr if at start
     */
    static StatusProvider* getPreviousDisplayableProvider(const StatusProvider* actual = nullptr, bool moveEnd = true);

    /**
     * Checks if the provider exists
     */
    static bool exists(const StatusProvider* provider);

    /**
     * Gets if this status provider is displayed on the OLED screen
     * @return true if the provider is displayed on the OLED screen
     */
    inline bool isOnScreen() const { return onScreen_; }

    /**
     * Sets if the provider is shown on the screen
     * @param onScreen True to signal the provider is on screen
     */
    inline void setOnScreen(bool onScreen) { onScreen_ = onScreen; }

    /**
     * Small comparator for OID values
     */
    class OIDComparator{
        public:
            bool operator()(const std::string& oid1, const std::string& oid2) const;
    };
    //Map containing SNMP data ordered by OID
    typedef std::map<std::string, const StatusData*, OIDComparator> SNMPDataMap;

    /**
     * Finds data with specified oid
     * @param oid SNMP OID to look for
     * @return pointer to found StatusData or nullptr if not found
     */
    static const StatusData* locateSNMPData(const char* oid);

    /**
     * Finds the next data after the one specified by oid
     * @param oid SNMP OID to look next
     * @param found Set to true if original data is found
     * @param onlyEnabled False to get next SNMP data event if it's disabled
     * @return pointer to next data (nullptr if not found)
     */
    static const StatusData* locateNextSNMPData(const char* oid, bool& found, bool onlyEnabled=true);

    /**
     * List known OID in the JSON document
     */
    static void listOIDInJSON(JsonDocument& doc);

    /**
     * Register an alarm callback
     * @param cb Callback called when an alarm occurs
     */
    static void registerAlarmListener(AlarmListener* cb);

    /**
     * Unregister an alarm callback
     * @param cb Callback to unregister
     */
    static void unregisterAlarmListener(AlarmListener* cb);

    typedef std::multimap<unsigned long, const StatusData*> DataCollection;
    /**
     * Gets list of active alarms
     */
    static void getActiveAlarms(DataCollection& col);

    /**
     * Gets most severe alarm severity
     * @return Most severe AlarmSeverity
     */
    static AlarmSeverity getMostAlarmSeverity();

    /*
    * Get most critical alarm (most severe, then most recent)
    */
    static void getMostCriticalAlarm(AlarmInfo& info);

protected:
    /**
     * Sets if the provider is enabled
     * @param enabled True to enable this provider
     */
    inline void setEnabled(bool enabled) { enabled_ = enabled; }

    /**
     * Append a StatusData to this provider collection
     */
    void addData(StatusData* data);

    /**
     * Build a lock to protect mutual access to a StatusData
     */
    DataLock lock() const;

    /**
     * Signal something changed
     */
    static void setProviderChanged();

    /**
     * Call when alarm status changed
     */
    static void alarmChanged(const StatusData* data);

private:
    SemaphoreHandle_t mutexData_;                           //!< Semaphore to protect mutual access
    StatusData* firstData_;                                 //!< Pointer to the first data in this collection
    const char* name_;                                      //!< Name of this provider
    bool enabled_;                                          //!< Gets if the provider is enabled
    bool onScreen_;                                         //!< Is this provider shown on screen
    typedef std::deque<StatusProvider*> StatusProviders;
    static StatusProviders* providers_;                     //!< Known providers list
    static SemaphoreHandle_t providersMutex_;               //!< Protect access to providers
    static bool providerChanged_;                           //!< True if something changed on a provider
    static SNMPDataMap* snmpData_;                          //!< SNMP data

    typedef std::vector<AlarmListener*> AlarmCallbacks;
    static AlarmCallbacks* alarmCallbacks_;                 //Alarm callbacks

    /**
     * List known OID
     */
    static const SNMPDataMap* listOID();

};

/**
 * Generic status data
 */
class StatusData
{
friend class StatusProvider;
public:
    /**
     * Constructor
     * @param provider Linked status provider
     * @param name Name of the data
     */
    StatusData(StatusProvider* provider, const char* name);

    /**
     * Constructor
     * @param provider Linked status provider
     * @param name Name of the data
     * @param unit Unit of the data
     */
    StatusData(StatusProvider* provider, const char* name, const char* unit);

    /**
     * Destructor
     */
    virtual ~StatusData() = default;

    /**
     * Returns a string representation of the data
     * @return a string representation of the data
     */
    virtual std::string toString() const = 0;

    /**
     * Gets next (chained) data, nullptr if no next data
     */
    inline const StatusData* getNext() const { return next_; }

    /**
     * Sets the next data followed by this
     */
    inline void setNext(StatusData* next) { next_ = next; }

    /**
     * Gets next item
     */
    inline StatusData* getNext() { return next_; }

    /**
     * Gets data name
     */
    inline const char* getName() const { return name_; }

    /**
     * Gets data unit
     */
    inline const char* getUnit() const { return unit_; }

    /**
     * Gets timestamp (uS timer) of the last data update
     *
     */
    int64_t getLastUpdate() const;

    /**
     * Gets if the data is enabled
     */
    inline bool isEnabled() const { return enabled_; }

    /**
     * Sets if the data is enabled
     */
    inline void setEnabled(bool enabled) { enabled_ = enabled; }

    /**
     * Gets if the data and it's parent provider ar enabled
     */
    bool isEffectivelyEnabled() const;

    /**
     * Gets if this data must be displayed on the screen
     * @return true if the data is displayable
     */
    inline bool isDisplayableOnScreen() const { return enabled_ && showOnScreen_; }

    /**
     * Sets if the data must be displayed
     * @param show true to display this on screen
     */
    inline void setDisplayableOnScreen(bool show) { showOnScreen_ = show; }

    /**
     * Gets SNMP OID associated with this data
     * @return SNMP OID or nullptr if the data is not SNMP enabled
     */
    inline const char* getSNMPOID() const { return snmpOID_; }

    /**
     * Sets SNMP OID
     * @param oid SNMP oid (static variable)
     */
    inline void setSNMPOID(const char* oid) { snmpOID_ = oid; }

    /**
     * Signal the client the change of data
     */
    inline void updated() {lastUpdate_ = esp_timer_get_time(); }

    /**
     * builds the SNMP BER containing this data
     * @return SNMP BER object or nullptr if no SNMP OID is declared
     */
    virtual SNMP::BER* buildSNMPBER() const = 0;

    /**
     * Gets parent provider
     * @return Pointer to parent StatusProvider
     */
    inline const StatusProvider* getProvider() const { return provider_; }

    /**
     * Sets alarm for this Status data
     * @param severity Alarm severity
     */
    void setAlarm(AlarmSeverity severity = AlarmSeverity::INACTIVE);

    /**
     * Sets alarm text
     */
    inline void setAlarmText(const char* text) { alarmInfo_.text = text; };

    /**
     * Clears alarm
     */
    void clearAlarm();

    /**
     * Gets alarm details
     * @param severity Alarm severity
     * @param text Alarm text
     * @param start Time of alarm raised
     * @return True if alarm is active
     */
    bool getAlarm(AlarmInfo& info) const;

    /**
     * Gets alarm severity
     * @return alarm severity
     */
    AlarmSeverity getAlarmSeverity() const;

    /**
     * Gets alarm time
     */
    inline unsigned long getAlarmTime() const { return alarmInfo_.startTime; };

    /**
     * Gets if alarm is active
     * @return true if alarm is active
     */
    inline bool isAlarmActive() const { return alarmInfo_.severity != AlarmSeverity::INACTIVE; }

    /**
     * Sets the text shown on OLED
     */
    inline void setOLEDText(const char* text) { oledText_ = text; }

    /**
     * Gets text to be displayed on OLED screen
     */
    inline const char* getOLEDText() const { return oledText_ ? oledText_ : name_; }

protected:
    inline DataLock lock() const { return provider_->lock(); }


    /**
     * Insert data into the JSON object
     * @param doc JSON object to fill
     */
    virtual void insertInJSON(JsonObject& doc) const = 0;

private:
    const char* name_;
    const char* unit_;
    const char* snmpOID_;
    const char* oledText_;
    int64_t lastUpdate_;
    bool enabled_;
    bool showOnScreen_;
    StatusProvider* provider_;
    StatusData* next_;
    AlarmInfo alarmInfo_;
};

/**
 * Status data implementation
 */
template<typename T>
class StatusValue : public StatusData
{
public:
    /**
     * Constructor with default value constructor
     * @param provider Linked status provider
     * @param name Name of the data
     */
    StatusValue(StatusProvider* provider, const char* name) : StatusData(provider, name) {
    }

    /**
     * Constructor with default value constructor
     * @param provider Linked status provider
     * @param name Name of the data
     * @param unit Unit of the data
     */
    StatusValue(StatusProvider* provider, const char* name, const char* unit) : StatusData{provider, name, unit} {
    }

    /**
     * Constructor with given value
     * @param provider Linked status provider
     * @param name Name of the data
     * @param unit Unit of the data
     * @param value Value of the data
     */
    StatusValue(StatusProvider* provider, const char* name, const char* unit, T value) : StatusData{provider, name, unit}, value_{value}
    {
    }

    /**
     * Constructor with given value
     * @param provider Linked status provider
     * @param name Name of the data
     * @param value Value of the data
     */
    StatusValue(StatusProvider* provider, const char* name, T value) : StatusData{provider, name}, value_{value}
    {
    }

    /**
     * Constructor with value getter
     * @param provider Linked status provider
     * @param name Name of the data
     * @param getter Getter function
     */
    StatusValue(StatusProvider* provider, const char* name, std::function<T()> getter) : StatusData{provider, name}, getter_{getter}
    {
    }

    /**
     * Constructor with value getter
     * @param provider Linked status provider
     * @param name Name of the data
     * @param unit Unit of the data
     * @param getter Getter function
     */
    StatusValue(StatusProvider* provider, const char* name, const char* unit, std::function<T()> getter) : StatusData{provider, name, unit}, getter_{getter}
    {
    }

    ~StatusValue() = default;

    /**
     * Gets value
     * @return Value of the data
     */
    const T getValue() const {
        if(getter_){
            return getter_();
        }
        DataLock l = StatusData::lock();
        return value_;
    }

    /**
     * Sets value
     * @param value Value to set
     */
    void setValue(T value) {
        DataLock l = StatusData::lock();
        if(value_ != value){
            value_ = value;
            StatusData::updated();
        }
    }

    /**
     * Sets value getter function
     * @param getter Getter function
     */
    inline void setGetter(std::function<T()> getter) { getter_ = getter; }

    /**
     * @see StatusData::toString() const
     */
    std::string toString() const override
    {
        std::ostringstream oss;
        oss << getValue();
        return oss.str();
    }

    /**
     * @see void StatusData::insertInJSON(JsonVariant& object) const
     */
    void insertInJSON(JsonObject& doc) const override
    {
        doc[getName()] = getValue();
    }

    /**
     * Copy operator
     */
    StatusValue<T>& operator=(const StatusValue<T>& other)
    {
        if(this == &other){
            return *this;
        }
        value_ = other.value_;
        name_ = other.name_;
    }

    /**
     * Not equal operator
     */
    bool operator!=(const StatusValue<T>& other)
    {
        return (name_ != other.name_) || (value_ != other.value_);
    }

    SNMP::BER* buildSNMPBER() const override
    {
        return nullptr;
    }

private:
    T value_;
    std::function<T()> getter_;
};

/**
 * Specialization for IPAddress
 */
template<>
std::string StatusValue<IPAddress>::toString() const;

template<>
void StatusValue<IPAddress>::insertInJSON(JsonObject& doc) const;

/**
 * Bool specialization
 */
template<>
std::string StatusValue<bool>::toString() const;

/**
 * Specialization for TimeTicks
 */
template<>
std::string StatusValue<TimeTicks>::toString() const;

template<>
void StatusValue<TimeTicks>::insertInJSON(JsonObject& object) const;

/**
 * SNMP BER specialization
 * TODO: Can we use template to create BER?
 */
template<>
SNMP::BER* StatusValue<bool>::buildSNMPBER() const;

template<>
SNMP::BER* StatusValue<int32_t>::buildSNMPBER() const;

template<>
SNMP::BER* StatusValue<float>::buildSNMPBER() const;

template<>
SNMP::BER* StatusValue<double>::buildSNMPBER() const;

template<>
SNMP::BER* StatusValue<IPAddress>::buildSNMPBER() const;

template<>
SNMP::BER* StatusValue<std::string>::buildSNMPBER() const;

template<>
SNMP::BER* StatusValue<TimeTicks>::buildSNMPBER() const;

/**
 * This class is used to represent a enum status value
 */
class StatusEnumValue : public StatusData
{
public:
    /**
     * Enum value/name pair
     */
    // struct EnumPair{
    //     int value;
    //     const char* name;
    //     EnumPair(int val, const char* n) : value{val}, name{n}{};
    // };
    typedef std::pair<int, const char*> EnumPair;
    /**
     * Constructor
     * @param provider Associated provide
     * @param name Data name
     * @param vals Enum pairs
     */
    StatusEnumValue(StatusProvider* provider, const char* name, std::initializer_list<EnumPair> vals);


    StatusEnumValue(StatusProvider* provider, const char* name, std::initializer_list<EnumPair> vals, std::function<int()> getter);

    int getValue() const;

    void setValue(int value);

    std::string toString() const override;

    void insertInJSON(JsonObject& doc) const override;

    virtual SNMP::BER* buildSNMPBER() const override;
private:
    int value_;
    std::initializer_list<EnumPair> valsPairs_;
    std::function<int()> getter_;
};


#endif