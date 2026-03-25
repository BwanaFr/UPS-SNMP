/*
**    This software license is not yet defined.
**
*/
#ifndef _USER_LED_HPP__
#define _USER_LED_HPP__
#ifdef RGB_LED_PIN
#include <Arduino.h>
#include <LedStrip.hpp>

/**
 * Simple class for managing user LED
*/
class UserLed  {
public:
    /**
     * Constructor
    */
    UserLed();

    ~UserLed();

    /**
     * Begin task
     */
    void begin();

    /**
     * Fatal error, nothing more to do
     */
    void fatal();

    /**
     * End of initialization
     */
    void endInitialization();

    /**
     * Sets custom color
     */
    void customColor(uint8_t r, uint8_t g, uint8_t b);

    /**
     * Sets custom color using HSV color space
     */
    void customHSV(float hue, float saturation, float value);

    /**
     * Dim the led
     */
    void dim(bool dim);

    /**
     * Gets pointer to this instance
     */
    static inline UserLed* getInstance(){  return instance_; }
private:
    enum class State {OFF,
                    INITIALIZING,   //Device is initializing
                    INIT_RESET_1,   //Reset key press 1
                    INIT_RESET_2,   //Reset key press 2
                    INIT_RESET_3,   //Reset key press 3
                    OK,
                    ERROR,
                    WARNING,
                    FATAL,
                    CUSTOM,
                    DIM,
                    UNDIM
                };

    State state_;                                               //!< Actual state
    struct NewState{
        State state;
        uint32_t payload;
    };                                                          //!< Message from queue
    LedStrip ledStrip_;                                         //!< Led strip object
    GRBPixel ledColor_;                                         //!< Actual LED Color
    GRBPixel customColor_;                                      //!< Custom color
    bool isOn_;                                                 //!< Is the LED on
    bool isDim_;                                                //!< Is the LED dimmed?
    TickType_t waitTicks_;                                      //!< Last LED change
    SemaphoreHandle_t semaphore_;                               //!< Semaphore to share ressource
    QueueHandle_t newStateQueue_;                               //!< Reference to message queue

    void setNewState(State newState, u_int32_t payload = 0);
    State getState();
    void loop();

    static UserLed* instance_;                                  //!< Our instance
    static void HSVToRGB(float hue, float saturation, float value, uint8_t& red, uint8_t& green, uint8_t& blue);
    static void ledTask(void* param);

    struct StateSettings{
        const LedStrip::ColorCode onColor;
        const LedStrip::ColorCode offColor;
        const unsigned long timeOn;
        const unsigned long timeOff;
    };

    static constexpr StateSettings STATE_SETTINGS[] = {
        {LedStrip::ColorCode::Black, LedStrip::ColorCode::Black, 0, 0},               //O`FF
        {LedStrip::ColorCode::Black, LedStrip::ColorCode::Blue, 50, 50},           //INITIALIZING
        {LedStrip::ColorCode::White, LedStrip::ColorCode::Black, 0, 0},         //INIT_RESET_1
        {LedStrip::ColorCode::White, LedStrip::ColorCode::Black, 200, 200},     //INIT_RESET_2
        {LedStrip::ColorCode::Black, LedStrip::ColorCode::Yellow, 100, 100},       //INIT_RESET_3
        {LedStrip::ColorCode::Green, LedStrip::ColorCode::Black, 50, 500},          //OK
        {LedStrip::ColorCode::Red, LedStrip::ColorCode::Black, 500, 500},         //ERROR
        {LedStrip::ColorCode::Orange, LedStrip::ColorCode::Black, 500, 500},     //WARNING
        {LedStrip::ColorCode::Red, LedStrip::ColorCode::Black, 200, 200},         //Fatal
    };

    static constexpr float DIM_SCALE = 25.0f;
};
#endif
#endif
