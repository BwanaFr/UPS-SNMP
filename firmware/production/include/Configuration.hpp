#ifndef _CONFIGURATION_HPP__
#define _CONFIGURATION_HPP__

#include <string>
#include <IPAddress.h>
#include <ArduinoJson.h>
#include <vector>
#include <functional>
#include <FreeRTOS.h>

class DeviceConfiguration {
public:
    /**
     * Enumeration of parameter
     */
    enum class Parameter {
        DEVICE_NAME = 0,            //Name of the device
        IP_CONFIGURATION,           //IP configuration
        SNMP_COMMUNITY,             //SNMP public community
        LOGIN_USER,                 //User login name
        LOGIN_PASS,                 //User password
        MAC_ADDRESS,                //MAC address
        PING_TEST_CONFIGURATION,    //Ping test configuration
        SWITCH_IP                   //IP address of switch to monitor
    };

    DeviceConfiguration();
    virtual ~DeviceConfiguration();

    /**
     * Setup
     */
    void begin();

    /**
     * Loads configuration from JSON file in flash
     */
    void load();

    /**
     * Sets the name of the device
     * @param name Name of the device
     * @param forceNotification True to notifiy new configuration (even if not changed)
     * @return true if changed
     */
    bool setDeviceName(const std::string& name, bool forceNotification = false);

    /**
     * Gets device name
     * @param name Name of the device
     */
    void getDeviceName(std::string& name);

    /**
     * Sets IP address configuration
     * @param useDHCP True to use DHCP client
     * @param ip IP Address of this device
     * @param subnet IP subnet mask
     * @param gateway First gateway IP
     * @param forceNotification True to notifiy new configuration (even if not changed)
     * @return true if changed
     */
    bool setIPConfiguration(bool useDHCP, const IPAddress& ip, const IPAddress& subnet, const IPAddress& gateway, bool forceNotification = false);

    /**
     * Gets IP address of this device
     * @param useDHCP True to use DHCP client
     * @param ip IP address of this device
     * @param subnet Subnet mask
     * @param gateway First gateway IP
     */
    void getIPConfiguration(bool& useDHCP, IPAddress& ip, IPAddress& subnet, IPAddress& gateway);

    /**
     * Gets the MAC address
     * @param mac MAC address to set
     */
    void getMACAddress(std::string& mac);

    /**
     * Sets the MAC address
     * @param mac MAC address to retrieve
     * @param forceNotification True to notifiy new configuration (even if not changed)
     * @return true if changed
     */
    bool setMACAddress(const std::string& mac, bool forceNotification = false);

    /**
     * Sets configuration user name
     * @param user New username to set
     * @param forceNotification True to notifiy new configuration (even if not changed)
     * @return true if changed
     */
    bool setUserName(const std::string& user, bool forceNotification = false);

    /**
     * Gets configuration user name
     */
    void getUserName(std::string& user);

    /**
     * Sets configuration password
     * @param password New password to set
     * @param forceNotification True to notifiy new configuration (even if not changed)
     * @return true if changed
     */
    bool setPassword(const std::string& password, bool forceNotification = false);

    /**
     * Gets configuraton password
     */
    void getPassword(std::string& password);

    /**
     * Sets SNMP public community string
     * @param comString Community string to set
     * @param forceNotification True to notifiy new configuration (even if not changed)
     * @return true if changed
     */
    bool setSNMPPublicCommunity(const std::string& comString, bool forceNotification = false);

    /**
     * Gets SNMP public community string
     * @param comString Community string to get
     */
    void getSNMPPublicCommunity(std::string& comString);

    /**
     * Sets IP to use for ping test
     * @param useGateway Use gateway IP
     * @param ip IP to use
     * @param interval Interval of ping request
     * @param timeout Timeout of ping request
     * @param forceNotification True to notifiy new configuration (even if not changed)
     * @return true if changed
     */
    bool setPingTestConfiguration(bool useGateway, const IPAddress& ip, int interval, int timeout, bool forceNotification = false);

    /**
     * Gets IP to use for ping test
     * @param useGateway Use gateway IP
     * @param ip IP to use
     * @param interval Interval of ping request
     * @param timeout Timeout of ping request
     */
    void getPingTestConfiguration(bool& useGateway, IPAddress& ip, int& interval, int& timeout);

   /**
     * Sets switch to be monitored IP address
     * @param address IP address of the switch
     * @param forceNotification True to notifiy new configuration (even if not changed)
     * @return true if changed
     */
    bool setSwitchIP(const IPAddress& address, bool forceNotification = false);

    /**
     * Gets switch IP address to be monitored
     * @param address IP address out parameter
     */
    void getSwitchIP(IPAddress& address);

    /**
     * Create a JSON string of the configuration
     */
    void toJSONString(std::string& str);

    /**
     * Fill configuration to JSON document
     * @param doc JSON document to fill
     * @param includePass True to include ciphered password
     */
    void toJSON(JsonDocument& doc, bool includeLogin = false);

    /**
     * Loads from JSON document
     * @param doc JSON document to load
     * @param changed True if the configuration changed
     * @param valid True if the configuration is valid
     * @param ipChanged True if IP configuration is modified
     * @param forceNotification True to notifiy new configuration (even if not changed)
     */
    void fromJSON(const JsonDocument& doc, bool& changed, bool& valid, bool& ipChanged, bool forceNotification = false);

    /**
     * Paramter change callback
     */
    typedef std::function<void(Parameter)> ParameterListener;

    /**
     * Register a new listener
     */
    void registerListener(ParameterListener listener);

    /**
     * Resets configuration to default value
     */
    void resetToDefault();

private:
    unsigned long lastChange_;                  //!< Last change of one setting
    SemaphoreHandle_t mutexData_;               //!< Protect access to ressources
    SemaphoreHandle_t mutexListeners_;          //!< Protect access to listeners vector
    std::string deviceName_;                    //!< Device name
    std::string userName_;                      //!< User name
    std::string password_;                      //!< Password
    bool useDHCP_;                              //!< Use DHCP for IP configuration
    IPAddress ip_;                              //!< Device IP if static
    IPAddress subnet_;                          //!< Device Subnet if static IP
    IPAddress gateway_;                         //!< Next gateway if static IP
    std::string snmpPublicCommmunity_;          //!< SNMP public community string
    IPAddress pingTestIP_;                      //!< Ping test IP address
    bool pingTestGateway_;                      //!< Ping test gateway?
    int pingTestInterval_;                      //!< Ping test interval [ms]
    int pingTestTimeout_;                       //!< Ping test timeout [ms]
    IPAddress switchIP_;                        //!< Switch monitoring IP address
    std::string macAddress_;                    //!< MAC address override
    bool lastButton_;                           //!< Last button state
    bool cfgReset_;                             //!< Configuration reseted
    bool ipCfgChanged_;                         //!< IP configuration changed
    unsigned long lastPress_;                   //!< Last button press
    std::vector<ParameterListener> listeners_;  //!< Configuration listeners
    TaskHandle_t configTask_;                   //!<< Handle to the task

    void notifyListeners(Parameter changed);

    /**
     * Write to flash
     */
    void write();

    /**
     * Loop to delay flash writing
     */
    void loop();

    /**
     * Helper to set configuration changed
     */
    void configurationChanged();

    static std::string encrypt(const std::string& input);
    static std::string decrypt(const std::string& input);
    static void generateKeys(unsigned char iv[16], unsigned char key[128]);
    static void configTask(void* param);
};

extern DeviceConfiguration configuration;

#endif