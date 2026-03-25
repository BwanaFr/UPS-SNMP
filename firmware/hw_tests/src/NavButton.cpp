#include <NavButton.hpp>
#include <Arduino.h>
#include <esp_log.h>


static const char* TAG = "NavButton";

NavButton::NavButton() : clickIO_(0xff), leftIO_(0xff), rightIO_(0xff), fastIO_(0xff), state_(State::IDLE)
{
    lastChange_ = millis();
}

void NavButton::begin(uint8_t clickIO, uint8_t leftIO, uint8_t rightIO, uint8_t fastIO)
{
    clickIO_ = clickIO;
    if(clickIO_ != 0xff){
        pinMode(clickIO_, INPUT);
    }
    leftIO_ = leftIO;
    if(leftIO_ != 0xff){
        pinMode(leftIO_, INPUT);
    }
    rightIO_ = rightIO;
    if(rightIO_ != 0xff){
        pinMode(rightIO_, INPUT);
    }
    fastIO_ = fastIO;
    if(fastIO_ != 0xff){
        pinMode(fastIO_, INPUT);
    }
}

void NavButton::loop()
{
    State newState = State::IDLE;
    bool fast = !digitalRead(fastIO_);
    if(!digitalRead(leftIO_)){
        newState = fast ? State::LEFT_FAST : State::LEFT;
    }else if(!digitalRead(rightIO_)){
        newState = fast ? State::RIGHT_FAST : State::RIGHT;
    }else if(!digitalRead(clickIO_)){
        newState = State::CLICKED;
    }
    if(newState != state_){
        state_ = newState;
        lastChange_ = millis();
    }
}

NavButton::State NavButton::getState(unsigned long& since) const
{
    since = millis() - lastChange_;
    return state_;
}