#ifndef _UI_HPP__
#define _UI_HPP__

#include <NavButton.hpp>
#include <StatusProvider.hpp>

#ifdef RGB_LED_PIN
#include <UserLed.hpp>
#endif

#ifndef NO_SCREEN
#include <Arduino.h>
#include <Adafruit_GFX.h>

//Exclamation mark bitmap
#define ALARM_BMP_WIDTH 32
#define ALARM_BMP_HEIGHT 32
const unsigned char ALARM_BMP [] PROGMEM=
{
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x03, 0xc0, 0x00, 0x00, 0x03, 0xc0, 0x00, 0x00, 0x07, 0xe0, 0x00, 0x00, 0x07, 0xe0, 0x00,
	0x00, 0x0f, 0xf0, 0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00, 0x1e, 0x78, 0x00, 0x00, 0x3e, 0x7c, 0x00,
	0x00, 0x3e, 0x7c, 0x00, 0x00, 0x7e, 0x7e, 0x00, 0x00, 0x7e, 0x7e, 0x00, 0x00, 0xfe, 0x7f, 0x00,
	0x01, 0xfe, 0x7f, 0x80, 0x01, 0xfe, 0x7f, 0x80, 0x03, 0xfe, 0x7f, 0xc0, 0x03, 0xfe, 0x7f, 0xc0,
	0x07, 0xff, 0xff, 0xe0, 0x0f, 0xff, 0xff, 0xf0, 0x0f, 0xfe, 0x7f, 0xf0, 0x1f, 0xfc, 0x3f, 0xf8,
	0x1f, 0xfc, 0x3f, 0xf8, 0x3f, 0xfe, 0x7f, 0xfc, 0x3f, 0xff, 0xff, 0xfc, 0x3f, 0xff, 0xff, 0xfc,
	0x1f, 0xff, 0xff, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

#ifdef SH110X
#include <Adafruit_SH110X.h>
#elif defined(CH1115)
#include <OLED_CH1115.h>
#else
#include <Adafruit_SSD1306.h>
#endif

#include <string>
#include <array>
#include <deque>

class StatusProvider;
class StatusData;
class IPAddress;

#ifdef SH110X
typedef Adafruit_SH1107 AdafruitOLED;
#define OLED_WHITE SH110X_WHITE
#define OLED_BLACK SH110X_BLACK
#elif defined(CH1115)
typedef OLED_CH1115 AdafruitOLED;
#define OLED_WHITE CH1115_WHITE
#define OLED_BLACK CH1115_BLACK
#else
typedef Adafruit_SSD1306 AdafruitOLED;
#define OLED_WHITE SSD1306_WHITE
#define OLED_BLACK SSD1306_BLACK
#endif

#define DATA_LINES 4

/**
 * This class renders a title on the OLED screen
 */
class HeaderRenderer {
public:
    /**
     * Constructor
     * @param display Pointer to the display object
     */
    HeaderRenderer();
    virtual ~HeaderRenderer();
protected:
    /**
     * To be called to render the header
     * @param display Pointer to the display object
     * @param force Force the display to be redrawn
     * @return true if display is changed
     */
    bool render(AdafruitOLED& display, bool force=false);

    /**
     * Sets the title text
     * @param display Pointer to the display object
     * @param text Text to be set (copied)
     */
    void setText(AdafruitOLED& display, const char* text);

private:
    GFXcanvas1* scrollCanevas_;
    uint16_t txtWidth_;
    int16_t scrollOffset_;
    int16_t scrollIncrement_;
    bool renderNext_;
    std::string text_;
    static constexpr int16_t SCROLL_AMOUNT = 5;
    static constexpr int16_t VERTICAL_SIZE = 12;
};

/**
 * Renderer for the status header
 */
class StatusHeaderRenderer : private HeaderRenderer {
public:
    /**
     * Constructor
     */
    StatusHeaderRenderer();
    virtual ~StatusHeaderRenderer();

    /**
     * Render the header
     * @param display Pointer to the display object
     * @param provider Provider to be rendered
     * @param force Force the display update
     * @return True if display is changed
     */
    bool render(AdafruitOLED& display, const StatusProvider* provider, bool force);
private:
    const StatusProvider* provider_;
};

/**
 * Renderer for one data (i.e. one line)
 */
class StatusDataRenderer{
public:
    StatusDataRenderer();
    virtual ~StatusDataRenderer();
    bool render(AdafruitOLED& display, const StatusData* data, bool force=false);
private:
    const StatusData* data_;
    int64_t lastDataUpdate_;
    GFXcanvas1* valueCanevas_;
    uint16_t valueStartX_;
    int16_t scrollOffset_;
    int16_t scrollIncrement_;
    int16_t scrollDelay_;
    static constexpr int16_t SCROLL_AMOUNT = 5;
    static constexpr int16_t SCROLL_DELAY = 3;
    static constexpr int16_t VERTICAL_SIZE = 11;
};

/**
 * Animate the logo
 */
class LogoAnimation
{
public:
    LogoAnimation();
    virtual ~LogoAnimation() = default;
    /**
     * Animates the logo
     * return true when animation is done
     */
    bool animate(AdafruitOLED& display, bool animate=true);

private:
    uint16_t step_;
    int16_t iterations_;
    unsigned long nextDelay_;
    unsigned long lastCall_;
};


/**
 * Configuration list
 */
class ConfigurationList
{
public:
    ConfigurationList();
    virtual ~ConfigurationList();

    /**
     * Loop
     * @param selection selected item (if selected) or -1
     * @param display OLED display to render
     * @param btn Navigation button
     * @param force Set to true to force rendering
     * @return true if screen needs refresh
     */
    bool loop(int& selection, AdafruitOLED& display, const NavButton& btn, bool force = false);

    /**
     * Adds an item in the list
     */
    void addItem(const char* item);

    /**
     * Removes all items from the list
     */
    void clearItems();

    /**
     * Sets dirty, need to be redrawn
     */
    inline void setDirty() { dirty_ = true; }
private:
    bool dirty_;                    //< Need to render
    int firstIndex_;                //< First index shown
    int selection_;                 //< Selected item
    std::deque<std::string> items_; //< Items list
    NavButton::State prevBtnState_; //< Previous boutton state
    static constexpr int NB_LINES = 4;
};

class IPConfiguration
{
public:
    IPConfiguration();
    virtual ~IPConfiguration() = default;

    /**
     * Loop
     * @param selection -1 if no selection, 0 to cancel, 1 to approve
     * @param display OLED display to render
     * @param btn Navigation button
     * @param name Name of the setting
     * @param force Force rendering
     * @return true if display needs to bne updated
     */
    bool loop(int& selection, AdafruitOLED& display, const NavButton& btn, const char* name, bool force);

    /**
     * Sets IP to show
     */
    void setIP(const IPAddress& ip);

    /**
     * Gets selected IP address
     */
    IPAddress getIP() const;

    /**
     * Method to only set one byte (auto cfg)
     */
    void setByte(uint8_t byte);

    /**
     * Gets first byte
     */
    inline uint8_t getByte() const { return bytes_[0]; }
private:
    int selected_;                  //< Actual selected digit
    uint8_t bytes_[4];              //< IP bytes
    uint8_t nbBytes_;               //< Number of bytes
    NavButton::State prevBtnState_; //< Previous boutton state
};

class BoolConfiguration
{
public:
    BoolConfiguration();
    virtual ~BoolConfiguration() = default;

    /**
     * Loop
     * @param selection -1 if no selection, 0 to cancel, 1 to approve
     * @param display OLED display to render
     * @param btn Navigation button
     * @param name Name of the setting
     * @param trueString String to show when true
     * @param falseString String to show when false
     * @param force Force rendering
     * @return true if screen needs refresh
     */
    bool loop(int& selection, AdafruitOLED& display, const NavButton& btn, const char* name,
        const char* trueString, const char* falseString, bool force);

    inline void setValue(bool value) { value_ = value; }
    inline bool getValue() const { return value_; }
private:
    int selected_;          //< Actual selected menu
    bool value_;            //< Actual value
    NavButton::State prevBtnState_; //< Previous boutton state
};

/**
 * This class is the configuration menu for the device
 * It allows to change IP address or perform a quick config of the device
 */
class ConfigurationMenu : private HeaderRenderer
{
public:
    ConfigurationMenu();
    virtual ~ConfigurationMenu() = default;
    /**
     * Loop
     * @return true when the configuration is over
     */
    bool loop(AdafruitOLED& display, const NavButton& btn, bool init);

private:
    enum class State{
        MAIN_MENU = 0,
        IP_MENU,
        AUTO_MENU,
        EXIT
    };
    bool mainMenu(State& newState, AdafruitOLED& display, const NavButton& btn, bool init);
    bool ipMenu(State& newState, AdafruitOLED& display, const NavButton& btn, bool init);
    bool autoMenu(State& newState, AdafruitOLED& display, const NavButton& btn, bool init);
    State state_;
    State prevState_;
    ConfigurationList cfgList_;
    IPConfiguration ipCfg_;
    BoolConfiguration boolCfg_;
};

/**
 * Main display class
 */
class Display : public AlarmListener
{
public:
    /**
     * Constructor
     * @param sclPin SCL I2C pin number
     * @param sdaPin SDA I2C pin number
     */
    Display(uint8_t sclPin, uint8_t sdaPin);

    /**
     * Default destructor
     */
    virtual ~Display() = default;

    /**
     * Begin the display
     */
    void begin();

    /**
     * Display loop
     */
    void loop(const NavButton &btn);

    /**
     * Display state
     */
    enum class State {
        STARTUP_LOGO = 0,   //!< Shows the startup logo
        STARTUP_DELAY,      //!< Waits a little
        NAVIGATION,         //!< User navigation with buttons
        SCREEN_SAVER,       //!< Screen saver (idle mode)
        CFG_PROMPT,         //!< Configuration menu prompt
        CFG_MENU,           //!< Configuration menu
    };

    void alarmChanged(const AlarmInfo& info) override;

    /**
     * Gets if the display is idle
     * @return true if the display is idle
     */
    inline bool isIdle() const { return state_ == State::SCREEN_SAVER; }

private:
    AdafruitOLED display_;                                      //!<< Display GFX object
    LogoAnimation* logoAnimation_;                              //!<< Logo animation object
    StatusHeaderRenderer statusHeader_;                         //!<< Status header renderer
    std::array<StatusDataRenderer, DATA_LINES> dataRenderers_;  //!<< lines data renderer
    StatusProvider* displayedProvider_ = nullptr;               //!<< Actually displayed provider
    State state_;                                               //!<< Display state
    ConfigurationMenu cfgMenu_;                                 //!<< Configuration menu
    TaskHandle_t taskHandle_;                                   //!<< Our task handle
    int displayContrast_;                                       //!< Display contrast
    bool alarmActive_;                                          //!< Is an alarm active

    /**
     * Displays the logo animation
     * @param animate True to make animation
     * @return True when animation is over
     */
    bool showLogo(bool animate = true);

    /**
     * Displays status data caroussel
     * @param btnLeft Button left clicked (rising edge)
     * @param btnRight Button right clicked (rising edge)
     * @param resume True when resuming from screen saver
     */
    void displayStatusData(bool btnLeft, bool btnRight, bool resume);

    /**
     * Fade screen out
     */
    bool fadeOut();

    /**
     * Fade screen in
     */
    bool fadeIn();

    /**
     * Displays the configuration prompt
     */
    void configurationPrompt(bool init);

    /**
     * Displays alarm(s), if active
     */
    bool alarmDisplay(bool init);

    /**
     * Array of StatusData pointers defining a page
     */
    typedef std::array<const StatusData*, DATA_LINES> Page;

    /**
     * Gets pages for specified provider
     * @param provider Data provider
     * @param number Page number wanted
     * @param page Array of StatusData making the page
     * @return true if something in page (good)
     */
    bool getPage(const StatusProvider* provider, int& number, Page& page);

    /**
     * Gets if more data to be displayed
     * @param data Next data to be displayed (null authorized)
     * @return true if next datas are displayable
     */
    bool hasMoreToDisplay(const StatusData* data);

    static constexpr unsigned long FADE_TIMEOUT = 30000;        ///< Fade timeout time
    static constexpr unsigned long CFG_PRESS_TIME = 2000;       ///< Press time to enter configuration prompt
    static constexpr unsigned long CFG_PROMPT_TIME = 3000;      ///< Configuration prompt time
    static constexpr unsigned long STARTUP_TIME = 5000;         ///< Time to display startup logo
    static constexpr uint8_t MAX_CONTRAST = 0x7F;               ///< Maximum screen contrast
    static constexpr uint8_t MIN_CONTRAST = 0x0;                ///< Fadded screen contrast
};



#endif  //NO_SCREEN

/**
 * Class for user interraction
 */
class UI
{
public:
    UI();
    virtual ~UI() = default;
    void begin();

    /**
     * Called when device got an IP address
     */
    void gotIP();

private:
enum class State : int {
    START = 0,      //MCU start
    WAIT_RESET,     //Wait for reset to be set
    CHECK_RESET,    //Check reset button
    ASK_RESET,      //Ask for reset
    VALIDATE_RESET, //Validates reset
    RUNNING         //Running state
};
    bool ipReceived_;   //IP received
    State state_;
#ifdef RGB_LED_PIN
    UserLed userLed_;
#endif
#ifndef NO_SCREEN
    Display display_;
#endif
    NavButton navButton_;
    NavButton::State btnState_;
    /**
     * UI FreeRTOS task function
     */
    static void uiTask(void* param);
    void loop();
};

extern UI ui;

#endif