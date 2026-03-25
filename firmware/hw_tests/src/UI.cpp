#include <UI.hpp>

static const char* TAG = "UI";


#ifndef NO_SCREEN
#include <Wire.h>

#include <DevicePins.hpp>

#include <esp_app_desc.h>

#ifdef SH110X
#define SCREEN_WIDTH 64     // OLED display width, in pixels
#define SCREEN_HEIGHT 128   // OLED display height, in pixels
#else
#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH 128    // OLED display width, in pixels
#endif
#ifndef SCREEN_HEIGHT
#define SCREEN_HEIGHT 64    // OLED display height, in pixels
#endif
#endif
#define OLED_RESET     -1   // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32 (0x3C for 128x64 SSD1315)


Display::Display(uint8_t sclPin, uint8_t sdaPin) :
    display_{SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1},
    taskHandle_{nullptr}, gotip_(false)
{
}

void Display::begin()
{
    Wire.setPins(SDA_OLED_PIN, SCL_OLED_PIN);   //TODO: Remove this to use the new driver when fixed (https://github.com/espressif/esp-idf/issues/15734)
    #ifdef SH110X
    if(!display_.begin(SCREEN_ADDRESS, true)){
        display_.setRotation(3);
    #elif defined(CH1115)
    if(!display_.begin(OLED_CH1115::SWITCHCAPVCC, SCREEN_ADDRESS, true, true)) {
    #else
    if(!display_.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    #endif
        ESP_LOGE(TAG, "OLED allocation failed");
        vTaskSuspend(NULL);
    }
    display_.setTextSize(1);            // Normal 1:1 pixel scale
    display_.setTextColor(OLED_WHITE);  // Draw white text
    display_.cp437(true);               // Use full 256 char 'Code Page 437' font
    display_.clearDisplay();
    display_.setCursor(0,0);
    display_.println("ESP-32 hardware test");
    display_.display();
}

void Display::loop(const NavButton &btn)
{
    static NavButton::State prevBtnState = NavButton::State::IDLE;
    unsigned long btnPressTime = 0;
    NavButton::State newBtnState = btn.getState(btnPressTime);
    bool update = false;
    if(newBtnState != prevBtnState){
        update = true;
    }
    if(gotip_){
        update = true;
        gotip_ = false;
    }
    if(update){
        std::string btnName;
        switch(newBtnState){
            case NavButton::State::CLICKED:
                btnName = "CLICKED";
                break;
            case NavButton::State::LEFT:
                btnName = "LEFT";
                break;
            case NavButton::State::LEFT_FAST:
                btnName = "LEFT FAST";
                break;
            case NavButton::State::RIGHT:
                btnName = "RIGHT";
                break;
            case NavButton::State::RIGHT_FAST:
                btnName = "RIGHT FAST";
                break;
            default:
                btnName = "---";
                break;
        }
        display_.clearDisplay();
        display_.setCursor(0,0);
        display_.println("ESP-32 hardware test");
        display_.println();
        display_.print("BUTTON : ");
        display_.println(btnName.c_str());
        display_.print("IP : ");
        display_.println(ipAddr_.empty() ? "---" : ipAddr_.c_str());
        display_.display();
    }

    prevBtnState = newBtnState;
}

void Display::setIP(const std::string& ip)
{
    ipAddr_ = ip;
    gotip_ = true;
}

#endif //NO_SCREEN

UI::UI() :
#ifndef NO_SCREEN
    display_{SCL_OLED_PIN, SDA_OLED_PIN},
#endif
    btnState_{NavButton::State::IDLE}
{
}

void UI::begin()
{
#ifdef RGB_LED_PIN
    userLed_.begin();
#endif

#ifndef NO_SCREEN
    display_.begin();
#endif

    navButton_.begin();

    //TODO: Review task prio & stack size
    xTaskCreatePinnedToCore(
        uiTask,
        "uiTask",
#ifndef NO_SCREEN
        8192,
#else
        2048,
#endif
        (void*)this,
        tskIDLE_PRIORITY + 1, NULL,
        ESP_TASK_MAIN_CORE ? 0 : 1
    );
}

void UI::uiTask(void* param)
{
    UI* ui = static_cast<UI*>(param);
    ui->loop();
    //Unreachable code as we are in for(;;) loop
    vTaskSuspend(NULL);
}

void UI::gotIP(const std::string& ip)
{
#ifndef NO_SCREEN
    display_.setIP(ip);
#endif
}

void UI::loop()
{
    unsigned long lastChange = 0;
    while(true){
        unsigned long now = millis();
        navButton_.loop();
        NavButton::State state = navButton_.getState();
        btnState_ = state;
#ifdef RGB_LED_PIN
        switch(btnState_){
            case NavButton::State::CLICKED:
                userLed_.customColor(0, 255, 0);
                break;
            case NavButton::State::LEFT:
                userLed_.customColor(255, 0, 0);
                break;
            case NavButton::State::LEFT_FAST:
                userLed_.customColor(255, 64, 64);
                break;
            case NavButton::State::RIGHT:
                userLed_.customColor(0, 0, 255);
                break;
            case NavButton::State::RIGHT_FAST:
                userLed_.customColor(64, 64, 255);
                break;
            default:
                userLed_.customColor(0,0,0);
                break;
        }
#endif
#ifndef NO_SCREEN
        display_.loop(navButton_);
#endif
        delay(10);
    }
}

UI ui;
