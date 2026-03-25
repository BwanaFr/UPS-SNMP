#ifndef _NAV_BUTTON_HPP__
#define _NAV_BUTTON_HPP__
#include <Arduino.h>
#include <atomic>

/**
 * Navigation button management
 */
class NavButton {
public:
    enum class State {
        IDLE,
        LEFT,
        RIGHT,
        LEFT_FAST,
        RIGHT_FAST,
        CLICKED,
        UNKNOWN
    };

    /**
     * Constuctor
     */
    NavButton();
    virtual ~NavButton() = default;

    /**
     * Configure navigation button
     * @param clickIO GPIO number for button click
     * @param leftIO GPIO number for up button
     * @param rightIO GPIO number for down button
     * @param fastIO GPIO number for fast button
     */
    void begin(uint8_t clickIO=42, uint8_t leftIO=18, uint8_t rightIO=15, uint8_t fastIO=0);

    /**
     * loop, must be called frequently to read button
     */
    void loop();

    /**
     * Gets button state
     * @param since set to number of ms the state is active
     * @return button state
     */
    State getState(unsigned long& since) const;

    inline State getState() const { return state_; }
private:
    uint8_t clickIO_;               //!< GPIO number for click
    uint8_t leftIO_;                //!< GPIO number for left
    uint8_t rightIO_;               //!< GPIO number for right
    uint8_t fastIO_;                //!< GPIO number for fast
    std::atomic<State> state_;                   //!< Actual state
    std::atomic<unsigned long> lastChange_;      //!< Last state change
};
#endif
