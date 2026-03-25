#ifndef _UI_HPP__
#define _UI_HPP__

#include <NavButton.hpp>

#ifdef RGB_LED_PIN
#include <UserLed.hpp>
#endif

#ifndef NO_SCREEN
#include <Arduino.h>
#include <Adafruit_GFX.h>


#ifdef SH110X
#include <Adafruit_SH110X.h>
#elif defined(CH1115)
#include <OLED_CH1115.h>
#else
#include <Adafruit_SSD1306.h>
#endif

#include <string>

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


/**
 * Main display class
 */
class Display
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

    void setIP(const std::string& ip);
private:
    AdafruitOLED display_;                                      //!<< Display GFX object
    TaskHandle_t taskHandle_;                                   //!<< Our task handle
    std::string ipAddr_;                                        //!<< IP address
    bool gotip_;                                                //!<< Got IP address
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
    void gotIP(const std::string& ip);

private:
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