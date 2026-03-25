#include <UI.hpp>

static const char* TAG = "UI";

#include <Configuration.hpp>

#ifndef NO_SCREEN
#include <Wire.h>

#include <Fonts/FreeSansBold9pt7b.h>
#include "Fonts/Dialog_bold_9.h"
#include "Fonts/DejaVu_Sans_Mono_11.h"

#include <DevicePins.hpp>
#include <StatusProvider.hpp>
#include <GitVersion.hpp>

//Product customization
#if defined __has_include
#  if __has_include (<Customization.hpp>)
#    include <Customization.hpp>
#  endif
#endif
#include <DefaultCustomization.hpp>
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




LogoAnimation::LogoAnimation() : step_(0), iterations_(0), nextDelay_(0), lastCall_(0)
{

}

bool LogoAnimation::animate(AdafruitOLED& display, bool doAnimation)
{
    static constexpr const int16_t barsHeight = 4;
#ifdef CUSTOM_LOGO
    static constexpr const int16_t logoHeight = custom_logo_height;
    static constexpr const int16_t logoWidth = custom_logo_width;
    static constexpr const unsigned char* logoBitmap = custom_logo;
#else
    static constexpr int16_t logoHeight = default_logo_height;
    static constexpr int16_t logoWidth = default_logo_width;
    static constexpr const unsigned char* logoBitmap = default_logo;
#endif

    if(!doAnimation){
        nextDelay_ = 0;
        step_ = 0;
    }

    if(millis() - lastCall_ > nextDelay_){
        lastCall_ = millis();
        switch(step_){
            case 0:
            {
                display.clearDisplay();
                std::string bottomText;
                if(!doAnimation){
                    configuration.getDeviceName(bottomText);
                }else{
                    bottomText = "Version ";
                    bottomText += GIT_VERSION;
                }
                int16_t x,y;
                uint16_t w,h;
                display.getTextBounds(bottomText.c_str(), 0, 0, &x, &y, &w, &h);
                display.setCursor(display.width()/2-w/2, logoHeight + (display.height() - logoHeight) / 2 - h/2);
                display.print(bottomText.c_str());
                step_ = 1;
                iterations_ = 0;
            }
            case 1:
            {
                int16_t x = display.width()/2 - logoWidth/2;
                display.drawBitmap(x, 0, logoBitmap, logoWidth, logoHeight, OLED_WHITE);
                if(doAnimation){
                    step_ = 2;
                }else{
                    step_ = 0;
                    break;
                }
            }
            case 2:
            {
                constexpr uint16_t nbBars = logoHeight / barsHeight;
                for(int i=0;i<nbBars;++i){
                    uint16_t h = barsHeight;
                    if((h + barsHeight*i) > logoHeight){
                        h = logoHeight - barsHeight*i;
                    }
                    if((i%2) == 0){
                        display.fillRect(0, barsHeight*i, display.width()/2 - iterations_, h, OLED_BLACK);
                    }else{
                        display.fillRect(display.width()/2 + iterations_, barsHeight*i, logoWidth/2 - iterations_, h, OLED_BLACK);
                    }
                }
                nextDelay_ = 20;
                if(++iterations_ >= logoWidth/2){
                    step_ = 0;
                }else{
                    step_ = 1;
                }
            }
            break;
            default:
            {
                step_ = 0;
                nextDelay_ = 0;
                iterations_ = 0;
                lastCall_ = 0;
            }
        }
        display.display();
    }
    return step_ == 0;
}

Display::Display(uint8_t sclPin, uint8_t sdaPin) :
    display_{SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1},
    logoAnimation_{nullptr},
    displayedProvider_{nullptr}, state_{State::STARTUP_LOGO},
    taskHandle_{nullptr}, displayContrast_{MAX_CONTRAST},
    alarmActive_{false}
{
    StatusProvider::registerAlarmListener(this);
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
}

bool Display::showLogo(bool animate)
{
    if(!logoAnimation_){
        logoAnimation_ = new LogoAnimation();
    }
    bool ret = logoAnimation_->animate(display_, animate);
    if(ret){
        delete logoAnimation_;
        logoAnimation_ = nullptr;
    }
    return ret;
}

void Display::loop(const NavButton &btn)
{
    static State prevState = state_;
    static NavButton::State prevBtnState = NavButton::State::IDLE;
    unsigned long btnPressTime = 0;
    NavButton::State newBtnState = btn.getState(btnPressTime);
    State nextState = state_;
    bool stateChanged = (prevState != state_);
    if(!taskHandle_){
        taskHandle_ = xTaskGetCurrentTaskHandle();
    }
    switch(state_){
        //Startup
        case State::STARTUP_LOGO:
        {
            fadeIn();
            if(showLogo(true)){
                nextState = State::STARTUP_DELAY;
            }
        }
        break;

        case State::STARTUP_DELAY:
        {
            if(btnPressTime > STARTUP_TIME){
                //Display alarms, if any active
                ulTaskNotifyTake(pdTRUE, 0);    //Consume all past alarm events
                nextState = State::SCREEN_SAVER;
            }
        }
        break;

        //Navigation menu
        case State::NAVIGATION:
        {
            bool btnRight = ((newBtnState == NavButton::State::RIGHT) || (newBtnState == NavButton::State::RIGHT_FAST)) && (prevBtnState == NavButton::State::IDLE);
            bool btnLeft = ((newBtnState == NavButton::State::LEFT) ||(newBtnState == NavButton::State::LEFT_FAST)) && (prevBtnState == NavButton::State::IDLE);
            displayStatusData(btnLeft, btnRight, stateChanged);
            if((newBtnState == NavButton::State::IDLE) && (btnPressTime > 30000)){
                //No press since 30 seconds, go in screen saver mode
                if(displayedProvider_){
                    displayedProvider_->setOnScreen(false);
                }
                nextState = State::SCREEN_SAVER;
            }
        }
        break;

        //Screen saver
        case State::SCREEN_SAVER:
        {
            alarmDisplay(stateChanged);
            if(!alarmActive_){
                //No alarms are active
                if(displayContrast_ == MAX_CONTRAST){
                    //Show logo and dim
                    showLogo(false);
                }
                fadeOut();  //Fade out will do nothing if already dimmed
            }

            if(newBtnState != NavButton::State::IDLE){
                //Button pressed maximize contrast
                fadeIn();
            }
            if((newBtnState == NavButton::State::CLICKED) && (btnPressTime > CFG_PRESS_TIME)){
                nextState = State::CFG_PROMPT;
            }else if((newBtnState == NavButton::State::IDLE) && (prevBtnState == NavButton::State::CLICKED) ||
                ((newBtnState != NavButton::State::CLICKED) && (newBtnState != NavButton::State::IDLE)))
            {
                nextState = State::NAVIGATION;
            }
        }
        break;

        //Configuration menu prompt
        case State::CFG_PROMPT:
        {
            configurationPrompt(stateChanged);
            if((newBtnState == NavButton::State::CLICKED) && (btnPressTime > (CFG_PROMPT_TIME + CFG_PRESS_TIME))){
                nextState = State::CFG_MENU;
            }else if(newBtnState != NavButton::State::CLICKED){
                nextState = State::SCREEN_SAVER;
            }
        }
        break;

        //Configuration menu
        case State::CFG_MENU:
        {
            if(cfgMenu_.loop(display_, btn, stateChanged)){
                nextState = State::SCREEN_SAVER;
            }
        }
        break;

        //Should not ge here
        default:
        {
            nextState = State::NAVIGATION;
        }
    }
    prevState = state_;
    state_ = nextState;
    prevBtnState = newBtnState;
}

void Display::alarmChanged(const AlarmInfo& info)
{
    if(taskHandle_){
        xTaskNotifyGive(taskHandle_);
    }
}

bool Display::getPage(const StatusProvider* provider, int& number, Page& page)
{
    int pageNumber = 0;
    const StatusData* nextData = provider ? provider->getFirstData() : nullptr;
    if(nextData){
        size_t i = 0;
        do{
            for(i=0;i<page.size();){
                if(nextData){
                    if(nextData->isDisplayableOnScreen()){
                        page[i] = nextData;
                        ++i;
                    }
                    nextData = nextData->getNext();
                }else{
                    break;
                }
            }
        }while((pageNumber++ != number) && hasMoreToDisplay(nextData));
        if(number < 0){
            number = pageNumber - 1;
        }
        //Clear all other pages
        for(;i<page.size();++i){
            page[i] = nullptr;
        }
    }
    bool lastPage = !hasMoreToDisplay(nextData);
    return lastPage;
}

bool Display::hasMoreToDisplay(const StatusData* data)
{
    if(data){
        const StatusData* d = data;
        while(d){
            if(d->isDisplayableOnScreen()){
                return true;
            }
            d = d->getNext();
        }
    }
    return false;
}

void Display::displayStatusData(bool btnLeft, bool btnRight, bool resume)
{
    static Page actualPage = {nullptr};
    static int pageNumber = 0;
    static unsigned long lastUpdate = 0;

    if(!StatusProvider::exists(displayedProvider_)){
        displayedProvider_ = StatusProvider::getNextDisplayableProvider();
        resume = true;
    }
    if(resume){
        display_.setTextSize(1);       // Normal 1:1 pixel scale
        display_.setTextColor(OLED_WHITE); // Draw white text
        display_.cp437(true);          // Use full 256 char 'Code Page 437' font
        display_.clearDisplay();
        display_.setTextWrap(false);
    }
    if(((millis() - lastUpdate) >= 50) || btnLeft || btnRight || resume){
        lastUpdate = millis();
        if(displayedProvider_){
            displayedProvider_->setOnScreen(true);
            bool update = false;
            display_.setCursor(0, 0);
            //Render title
            update |= statusHeader_.render(display_, displayedProvider_, resume);
            //Skip 2 lines
            display_.setCursor(0, display_.getCursorY() + 2);
            //Render data
            bool lastPage = getPage(displayedProvider_, pageNumber, actualPage);
            for(size_t i=0;i<dataRenderers_.size(); ++i){
                update |= dataRenderers_[i].render(display_, actualPage[i], resume);
            }

            if(btnRight){
                if(lastPage){
                    pageNumber = 0;
                    //No more pages to display, move to next provider
                    //Move next
                    StatusProvider* nextProvider = StatusProvider::getNextDisplayableProvider(displayedProvider_);
                    if(nextProvider != displayedProvider_){
                        displayedProvider_->setOnScreen(false);
                        displayedProvider_ = nextProvider;
                        if(displayedProvider_){
                            displayedProvider_->setOnScreen(true);
                        }
                    }
                }else{
                    ++pageNumber;
                }
            }else if(btnLeft){
                --pageNumber;
                if(pageNumber < 0){
                    StatusProvider* nextProvider = StatusProvider::getPreviousDisplayableProvider(displayedProvider_);
                    if(nextProvider != displayedProvider_){
                        displayedProvider_->setOnScreen(false);
                        displayedProvider_ = nextProvider;
                        if(displayedProvider_){
                            displayedProvider_->setOnScreen(true);
                        }
                        //Gets last page number of the provider by providing a negative page number
                        pageNumber = -1;
                    }
                }
            }

            if(update){
                display_.display();
            }
        }
    }
}

bool Display::fadeOut()
{

#if defined(SH110X) || defined(CH1115)
    if(displayContrast_ == MIN_CONTRAST){
        //Already dimed
        return true;
    }
    display_.setContrast(displayContrast_);
    if(--displayContrast_ == MIN_CONTRAST){
        return true;
    }
    return false;
#else
    display_.dim(true);
    return true;
#endif
}

bool Display::fadeIn()
{
displayContrast_ = MAX_CONTRAST;
#if defined(SH110X) || defined(CH1115)
    display_.setContrast(displayContrast_);
#else
    display_.dim(false);
#endif
return true;
}

void Display::configurationPrompt(bool init)
{
    static unsigned long startTime = 0;
    static uint16_t prevPbValue = 0;
    if(init){
        startTime = millis();
        prevPbValue = 0;
        display_.clearDisplay();
        display_.setCursor(0, 0);
        display_.setFont(&Dialog_bold_9);
        int16_t x,y;
        uint16_t h,w;
        int16_t dispSlices = display_.getCursorY() + 5;

        const String str1 = "Keep pressed";
        display_.getTextBounds(str1, 0, 0, &x, &y, &w, &h);
        display_.setCursor(display_.width() / 2 - w/2, dispSlices);
        display_.print(str1);

        const String str2 = "to enter";
        display_.getTextBounds(str2, 0, 0, &x, &y, &w, &h);
        display_.setCursor(display_.width() / 2 - w/2, dispSlices * 2);
        display_.print(str2);

        const String str3 = "configuration";
        display_.getTextBounds(str3, 0, h+2, &x, &y, &w, &h);
        display_.setCursor(display_.width() / 2 - w/2, dispSlices * 3);
        display_.print(str3);
        display_.setFont();

        display_.drawRect(2, display_.height() - 10, display_.width() - 4, 8, OLED_WHITE);
        display_.display();
    }

    unsigned long elapsed = (millis() - startTime);
    uint16_t pbTotalWidth = display_.width() - 8;
    double pbPercent = ((double)elapsed/(double)CFG_PROMPT_TIME);
    uint16_t pbWidth = (uint16_t)(pbPercent* (double)pbTotalWidth);
    if(prevPbValue != pbWidth){
        display_.fillRect(4, display_.height() - 8, pbWidth, 4, OLED_WHITE);
        display_.display();
        prevPbValue = pbWidth;
    }
}

bool Display::alarmDisplay(bool init)
{
    uint32_t notifyTaken = ulTaskNotifyTake(pdFALSE, 0);
    if(notifyTaken || init){    //Update display if alarm received or if init
        AlarmInfo info;
        StatusProvider::getMostCriticalAlarm(info);
        if(info.severity == AlarmSeverity::INACTIVE){
            alarmActive_ = false;
            return false;
        }else{
            alarmActive_ = true;
            fadeIn();
            display_.clearDisplay();
            display_.drawBitmap(0,0,ALARM_BMP, ALARM_BMP_WIDTH, ALARM_BMP_HEIGHT, OLED_WHITE);
            display_.setCursor(ALARM_BMP_WIDTH + 2, ALARM_BMP_HEIGHT / 2);
            display_.setFont(&DejaVu_Sans_Mono_11);
            int16_t x,y;
            uint16_t w,h;
            const char* severityStr = nullptr;
            switch(info.severity){
                case AlarmSeverity::INFO:
                    severityStr = "INFO";
                    break;
                case AlarmSeverity::WARNING:
                    severityStr = "WARNING";
                    break;
                case AlarmSeverity::ERROR:
                    severityStr = "ERROR";
                    break;
            }
            if(severityStr){
                display_.getTextBounds(severityStr, 0, 0, &x, &y, &w, &h);
                int16_t cursX = (display_.width() - ALARM_BMP_WIDTH) / 2 + ALARM_BMP_WIDTH -  w / 2;
                display_.setCursor(cursX, (ALARM_BMP_HEIGHT / 2) + h / 2);
                display_.print(severityStr);
            }
            display_.setFont();
            display_.getTextBounds(info.text.c_str(), 0, 0, &x, &y, &w, &h);
            display_.setCursor(display_.width()/2-w/2, (display_.height() - ALARM_BMP_HEIGHT) / 2 + ALARM_BMP_HEIGHT - h);
            display_.print(info.text.c_str());
            display_.display();
            return true;
        }
    }
    return false;
}

HeaderRenderer::HeaderRenderer() : scrollCanevas_{nullptr},
                    txtWidth_{0}, scrollIncrement_(SCROLL_AMOUNT),
                    renderNext_{false}
{
}

HeaderRenderer::~HeaderRenderer()
{
    if(scrollCanevas_){
        delete scrollCanevas_;
    }
}

bool HeaderRenderer::render(AdafruitOLED& display, bool force)
{
    constexpr int fontBaseLine = 11;
    bool ret = false;
    int16_t startY = display.getCursorY();
    if(scrollCanevas_ && (txtWidth_ > display.width())){
        //Copy pixel per pixel. Didn't find other reliable way to do
        for(int x=0;x<display.width();++x){
            for(int y=0;y<scrollCanevas_->height();++y){
                display.drawPixel(x, y + startY, scrollCanevas_->getPixel(x + scrollOffset_, y));
            }
        }
        scrollOffset_ += scrollIncrement_;
        //TODO: Wait a little before scrolling back?
        if(scrollOffset_ <= 0){
            scrollIncrement_ = SCROLL_AMOUNT;
            scrollOffset_ = 0;
        }else if(scrollOffset_ >= (txtWidth_ - display.width())){
            scrollIncrement_ = -SCROLL_AMOUNT;
            scrollOffset_ = (txtWidth_ - display.width());
        }
        ret = true;
    }else if(renderNext_ || force){
        //No need to scroll, center text
        display.fillRect(0, startY, display.width(), VERTICAL_SIZE, OLED_WHITE);
        display.setTextColor(OLED_BLACK);
        display.setFont(&Dialog_bold_9);
        display.setCursor(display.width() / 2 - txtWidth_ / 2, startY + VERTICAL_SIZE - 3);
        display.print(text_.c_str());
        display.setFont();
        ret = true;
    }
    display.setCursor(0, startY + VERTICAL_SIZE);
    renderNext_ = false;
    return ret;
}

void HeaderRenderer::setText(AdafruitOLED& display, const char* text)
{
    if(text_ != text){
        text_ = text;
        int16_t x1, y1;
        uint16_t h;
        display.setFont(&Dialog_bold_9);
        display.getTextBounds(text_.c_str(), 0, 0, &x1, &y1, &txtWidth_, &h);
        display.setFont();
        if(scrollCanevas_){
            delete scrollCanevas_;
            scrollCanevas_ = nullptr;
        }
        if(txtWidth_ <= display.width()){
            //No need to scroll, center text
            renderNext_ = true;    //No need to render more
        }else{
            //Will scroll, create a canevas to render
            scrollCanevas_ = new GFXcanvas1(txtWidth_, VERTICAL_SIZE);
            scrollCanevas_->setFont(&Dialog_bold_9);
            scrollCanevas_->fillRect(0, 0, scrollCanevas_->width(), VERTICAL_SIZE, OLED_WHITE);
            scrollCanevas_->setTextColor(OLED_BLACK);
            scrollCanevas_->setCursor(0, VERTICAL_SIZE - 3);
            scrollCanevas_->print(text_.c_str());
            scrollIncrement_ = SCROLL_AMOUNT;
            scrollOffset_ = 0;
        }
    }
}

StatusHeaderRenderer::StatusHeaderRenderer() : provider_{nullptr}
{
}

StatusHeaderRenderer::~StatusHeaderRenderer()
{
}

bool StatusHeaderRenderer::render(AdafruitOLED& display, const StatusProvider* provider, bool force)
{
    if(provider != provider_){
        //New provider, update text
        provider_ = provider;
        setText(display, provider_->getName());
    }
    return HeaderRenderer::render(display, force);
}

StatusDataRenderer::StatusDataRenderer() :
                    data_{nullptr}, lastDataUpdate_{0},
                    valueCanevas_{nullptr},
                    valueStartX_{0}, scrollOffset_{0},
                    scrollIncrement_(SCROLL_AMOUNT),
                    scrollDelay_{0}
{
}

StatusDataRenderer::~StatusDataRenderer()
{
}

bool StatusDataRenderer::render(AdafruitOLED& display, const StatusData* data, bool force)
{
    int16_t startingY = display.getCursorY();
    bool ret = false;
    if(force || (data != data_)){
        //New data, update name
        data_ = data;
        //Clear
        display.fillRect(0, startingY, display.width(), VERTICAL_SIZE, OLED_BLACK);
        if(data_){
            display.setFont();
            display.setCursor(0, startingY + 1);
            display.setTextColor(OLED_WHITE);
            display.print(data_->getOLEDText());
            display.print(':');
            valueStartX_ = display.getCursorX();
            lastDataUpdate_ = -1;
            scrollIncrement_ = SCROLL_AMOUNT;
            scrollOffset_ = 0;
            display.setCursor(0, startingY);
        }else{
            //Screen cleared, nothing to show, move next
            if(valueCanevas_){
                delete valueCanevas_;
                valueCanevas_ = nullptr;
            }
            ret =  true;
        }
    }
    bool render = false;
    if(data_ && (data_->getLastUpdate() != lastDataUpdate_)){
        //Render value
        lastDataUpdate_ = data_->getLastUpdate();
        std::string strValue = data_->toString();
        if(data_->getUnit()){
            if(data_->getUnit()[0] == 'C'){
                strValue += " ";
                strValue[strValue.length()-1] = 0xF7;
                strValue += "C";
            }else{
                strValue += data_->getUnit();
            }
        }
        int16_t x1, y1;
        uint16_t h, w;
        display.getTextBounds(strValue.c_str(), 0, 0, &x1, &y1, &w, &h);
        if(valueCanevas_){
            delete valueCanevas_;
            valueCanevas_ = nullptr;
        }
        valueCanevas_ = new GFXcanvas1{w, h};
        valueCanevas_->print(strValue.c_str());
        render = true;
    }
    bool needScrolling = false;
    //Check if scrolling is needed
    if(valueCanevas_ && (valueCanevas_->width() > (display.width()-valueStartX_))){
        needScrolling = true;
    }
    if(render || needScrolling){
        //Clear
        display.fillRect(valueStartX_, startingY, display.width(), VERTICAL_SIZE, OLED_BLACK);
        if(valueCanevas_){
            //Render canevas
            int maxX = valueStartX_ + valueCanevas_->width();
            if(maxX > display.width()){
                maxX = display.width();
            }

            int16_t scrollAmount = scrollOffset_;
            if(scrollAmount < 0){
                scrollAmount = 0;
            }else if(scrollAmount > (valueCanevas_->width() - (display.width() - valueStartX_))){
                if(needScrolling){ //Remove this condition for right alignement :)
                    scrollAmount = valueCanevas_->width() - (display.width() - valueStartX_);
                }else{
                    scrollAmount = 0;
                }
            }
            for(int x=valueStartX_; x<display.width();++x){
                int canevasX = (x - valueStartX_) + scrollAmount;
                for(int y=0;y<valueCanevas_->height();++y){
                    if(canevasX <= valueCanevas_->width()){
                        display.drawPixel(x, y + startingY + 1, valueCanevas_->getPixel(canevasX, y));
                    }else{
                        display.drawPixel(x, y + startingY + 1, OLED_BLACK);
                    }
                }
            }

            if(needScrolling){
                scrollOffset_ += scrollIncrement_;
                if(scrollOffset_ < -SCROLL_DELAY*SCROLL_AMOUNT){
                    scrollIncrement_ = SCROLL_AMOUNT;
                    scrollOffset_ = 0;
                }else if(scrollOffset_ > (valueCanevas_->width() - (display.width() - valueStartX_) + SCROLL_DELAY*SCROLL_AMOUNT)){
                    scrollIncrement_ = -SCROLL_AMOUNT;
                    scrollOffset_ = valueCanevas_->width() - (display.width() - valueStartX_);
                }
            }
        }
        ret = true;
    }

    //Move to next line
    display.setCursor(0, startingY + VERTICAL_SIZE);
    return ret;
}


ConfigurationList::ConfigurationList() : dirty_{false}, firstIndex_{-1}, selection_{0}, prevBtnState_{NavButton::State::UNKNOWN}
{
}

ConfigurationList::~ConfigurationList()
{
}

bool ConfigurationList::loop(int& selection, AdafruitOLED& display, const NavButton& btn, bool force)
{
    bool update = force || dirty_;
    if(firstIndex_ < 0){
        update = true;
        selection_ = 0;
        firstIndex_ = 0;
    }

    if((prevBtnState_ == NavButton::State::IDLE) && (btn.getState() == NavButton::State::RIGHT)){
        ++selection_;
        if(selection_ >= items_.size()){
            selection_ = 0;
            firstIndex_ = 0;
        }
        if((selection_ - firstIndex_) >= NB_LINES){
            ++firstIndex_;
        }
        update = true;
    }else if((prevBtnState_ == NavButton::State::IDLE) && (btn.getState() == NavButton::State::LEFT)){
        --selection_;
        if(selection_ < 0){
            selection_ = items_.size() - 1;
            firstIndex_ = (selection_ / NB_LINES) * NB_LINES - 1;
        }
        if(selection_ < firstIndex_){
            --firstIndex_;
        }
        update = true;
    }

    if(update){
        display.setFont();
        int16_t x = display.getCursorX();
        int16_t y = display.getCursorY();
        display.fillRect(display.getCursorX(), display.getCursorY(), display.width(), display.height() - display.getCursorX(), OLED_BLACK);
        display.setCursor(x, y + 4);
        display.setTextColor(OLED_WHITE);
        for(int i=0;i<NB_LINES;++i){
            int realIndex = firstIndex_ + i;
            if(realIndex < items_.size()){
                if(realIndex == selection_){
                    display.write(0x10);
                }else{
                    display.print(' ');
                }
                display.setCursor(display.getCursorX() + 2, display.getCursorY());
                display.println(items_[realIndex].c_str());
                display.setCursor(display.getCursorX(), display.getCursorY() + 2);
            }else{
                break;
            }
        }
        if(firstIndex_ > 0){
            uint16_t h,w;
            int16_t x1, y1;
            display.getTextBounds("O", 0, 0, &x1, &y1, &w, &h);
            display.setCursor(display.width()-w, y + 2);
            display.write(0x1E);
        }
        if((items_.size() - firstIndex_) > NB_LINES){
            uint16_t h,w;
            int16_t x1, y1;
            display.getTextBounds("O", 0, 0, &x1, &y1, &w, &h);
            display.setCursor(display.width()-w, display.height() - 2 - h);
            display.write(0x1F);
        }
        dirty_ = false;
    }
    selection = -1;
    if((btn.getState() == NavButton::State::CLICKED) && (prevBtnState_ == NavButton::State::IDLE)){
        selection = selection_;
    }
    prevBtnState_ = btn.getState();
    return update;
}

void ConfigurationList::addItem(const char* item)
{
    firstIndex_ = -1;   //Clear selection
    selection_ = 0;
    items_.push_back(item);
}

void ConfigurationList::clearItems()
{
    items_.clear();
    firstIndex_ = -1;   //Clear selection
    selection_ = 0;
}

IPConfiguration::IPConfiguration() : selected_{0}, bytes_{255,255,255,255}, nbBytes_{4},
                                    prevBtnState_{NavButton::State::UNKNOWN}
{
}

bool IPConfiguration::loop(int& selection, AdafruitOLED& display, const NavButton& btn, const char* name, bool force)
{
    unsigned long pressTime = 0;
    NavButton::State newState = btn.getState(pressTime);
    bool increment = false;
    bool decrement = false;
    selection = -1;
    if((selected_ > -1) && (selected_ < nbBytes_)){
        //Rising edge on right or fast clicked for long time
        if(((prevBtnState_ == NavButton::State::IDLE) && (newState == NavButton::State::RIGHT))  ||
            ((newState == NavButton::State::RIGHT_FAST) && (pressTime > 1000)))
        {
            bytes_[selected_]++;
            force = true;
        }else if(((prevBtnState_ == NavButton::State::IDLE) && (newState == NavButton::State::LEFT))  ||
            ((newState == NavButton::State::LEFT_FAST) && (pressTime > 1000)))
        {
            bytes_[selected_]--;
            force = true;
        }else if((prevBtnState_ == NavButton::State::IDLE) && (newState == NavButton::State::CLICKED)){
            selected_++;
            force = true;
        }
    }else if((prevBtnState_ == NavButton::State::IDLE) && (newState == NavButton::State::RIGHT)){
        //OK -> CANCEL
        selected_++;
        if(selected_ > (nbBytes_ + 1)){
            selected_ = 0;
        }
        force = true;
    }else if((prevBtnState_ == NavButton::State::IDLE) && (newState == NavButton::State::LEFT)){
        //CANCEL -> OK
        selected_--;
        force = true;
    }else if((prevBtnState_ == NavButton::State::IDLE) && (newState == NavButton::State::CLICKED)){
        if(selected_ == nbBytes_){
            //OK click
            prevBtnState_ = newState;
            selection = 1;
            return false;
        }else if(selected_ == (nbBytes_ + 1)){
            //CANCEL click
            prevBtnState_ = newState;
            selection = 0;
            return false;
        }
    }

    if(force){
        display.setFont();
        int16_t startX = display.getCursorX();
        int16_t startY = display.getCursorY();
        display.fillRect(display.getCursorX(), display.getCursorY(), display.width(), display.height() - display.getCursorX(), OLED_BLACK);
        display.setTextColor(OLED_WHITE);

        int16_t x,y;
        uint16_t h,w;
        display.getTextBounds(name, 0, 0, &x, &y, &w, &h);
        x = display.width()/2 - w/2;
        display.setCursor(x, startY + 4);
        display.print(name);
        display.setCursor(x, display.getCursorY() + h + 4);

        uint16_t digitW, digitH, dotW;
        display.getTextBounds("000", 0, 0, &x, &y, &digitW, &digitH);
        display.getTextBounds(".", 0, 0, &x, &y, &dotW, &h);

        w = digitW * nbBytes_ + dotW * (nbBytes_-1);
        //Center IP text
        x = (display.width() - w) / 2;
        display.setCursor(x, display.getCursorY() + 4);

        for(int i=0;i<nbBytes_;++i){
            if(i == selected_){
                display.fillRect(display.getCursorX(), display.getCursorY() - 1, digitW, digitH + 1, OLED_WHITE);
                display.setTextColor(OLED_BLACK);
            }else{
                display.fillRect(display.getCursorX(), display.getCursorY() - 1, digitW, digitH + 1, OLED_BLACK);
                display.setTextColor(OLED_WHITE);
            }
            display.printf("%03u", bytes_[i]);
            if(i<(nbBytes_-1)){
                display.setTextColor(OLED_WHITE);
                display.print('.');
            }
        }

        display.getTextBounds("OK", 0, 0, &x, &y, &w, &h);
        x = display.width() / 4 - w/2;
        startY = display.height() - h - 2;
        display.setCursor(x, startY);
        if(selected_ == nbBytes_){
            display.fillRect(display.getCursorX() - 1, display.getCursorY() - 1, w + 1, h + 1, OLED_WHITE);
            display.setTextColor(OLED_BLACK);
        }else{
            display.fillRect(display.getCursorX() - 1, display.getCursorY() - 1, w + 1, h + 1, OLED_BLACK);
            display.setTextColor(OLED_WHITE);
        }
        display.print("OK");

        display.getTextBounds("CANCEL", 0, 0, &x, &y, &w, &h);
        x = (display.width() / 4)*3 - w/2;
        display.setCursor(x, startY);
        if(selected_ == (nbBytes_ + 1)){
            display.fillRect(display.getCursorX() - 1, display.getCursorY() - 1, w + 1, h + 1, OLED_WHITE);
            display.setTextColor(OLED_BLACK);
        }else{
            display.fillRect(display.getCursorX() - 1, display.getCursorY() - 1, w + 1, h + 1, OLED_BLACK);
            display.setTextColor(OLED_WHITE);
        }
        display.print("CANCEL");
    }
    prevBtnState_ = newState;
    return force;
}

void IPConfiguration::setIP(const IPAddress& ip)
{
    nbBytes_ = 4;
    for(int i=0;i<nbBytes_;++i){
        bytes_[i] = ip[i];
    }
    selected_ = 0;
}

/**
 * Gets selected IP address
 */
IPAddress IPConfiguration::getIP() const
{
    return IPAddress(bytes_);
}

void IPConfiguration::setByte(uint8_t byte)
{
    nbBytes_ = 1;
    bytes_[0] = byte;
    selected_ = 0;
}

BoolConfiguration::BoolConfiguration() : selected_{0}, value_{false}, prevBtnState_{NavButton::State::UNKNOWN}
{
}

bool BoolConfiguration::loop(int& selection, AdafruitOLED& display, const NavButton& btn, const char* name,
                            const char* trueString, const char* falseString, bool force)
{
    NavButton::State newState = btn.getState();
    selection = -1;
    if((prevBtnState_ == NavButton::State::IDLE) && (newState == NavButton::State::RIGHT)){
        //OK -> CANCEL
        selected_++;
        if(selected_ > 2){
            selected_ = 0;
        }
        force = true;
    }else if((prevBtnState_ == NavButton::State::IDLE) && (newState == NavButton::State::LEFT)){
        //CANCEL -> OK
        selected_--;
        if(selected_ < 0){
            selected_ = 2;
        }
        force = true;
    }else if((prevBtnState_ == NavButton::State::IDLE) &&(newState == NavButton::State::CLICKED)){
        if(selected_ == 0){
            value_ = !value_;
            force = true;
        }else if(selected_ == 1){
            //OK click
            selection = 1;
            return false;
        }else if(selected_ == 2){
            //CANCEL click
            selection = 0;
            return false;
        }
    }

    if(force){
        display.setFont();
        int16_t startX = display.getCursorX();
        int16_t startY = display.getCursorY();
        display.fillRect(display.getCursorX(), display.getCursorY(), display.width(), display.height() - display.getCursorX(), OLED_BLACK);
        display.setTextColor(OLED_WHITE);

        int16_t x,y;
        uint16_t h,w;
        display.getTextBounds(name, 0, 0, &x, &y, &w, &h);
        x = display.width()/2 - w/2;
        display.setCursor(x, startY + 4);
        display.print(name);
        display.setCursor(x, display.getCursorY() + h + 4);
        const char* strValue = value_ ? trueString : falseString;
        display.getTextBounds(strValue, 0, 0, &x, &y, &w, &h);
        x = display.width() / 2 - w/2;
        display.setCursor(x, display.getCursorY() + 2);
        if(selected_ == 0){
            display.fillRect(display.getCursorX(), display.getCursorY() - 1, w, h + 1, OLED_WHITE);
            display.setTextColor(OLED_BLACK);
        }else{
            display.fillRect(display.getCursorX(), display.getCursorY() - 1, w, h + 1, OLED_BLACK);
            display.setTextColor(OLED_WHITE);
        }
        display.print(strValue);

        display.getTextBounds("OK", 0, 0, &x, &y, &w, &h);
        x = display.width() / 4 - w / 2;
        startY = display.height() - h - 2;
        display.setCursor(x, startY);
        if(selected_ == 1){
            display.fillRect(display.getCursorX() - 1, display.getCursorY() - 1, w + 1, h + 1, OLED_WHITE);
            display.setTextColor(OLED_BLACK);
        }else{
            display.fillRect(display.getCursorX() - 1, display.getCursorY() - 1, w + 1, h + 1, OLED_BLACK);
            display.setTextColor(OLED_WHITE);
        }
        display.print("OK");

        display.getTextBounds("CANCEL", 0, 0, &x, &y, &w, &h);
        x = (display.width() / 4)*3 - w/2;
        display.setCursor(x, startY);
        if(selected_ == 2){
            display.fillRect(display.getCursorX() - 1, display.getCursorY() - 1, w + 1, h + 1, OLED_WHITE);
            display.setTextColor(OLED_BLACK);
        }else{
            display.fillRect(display.getCursorX() - 1, display.getCursorY() - 1, w + 1, h + 1, OLED_BLACK);
            display.setTextColor(OLED_WHITE);
        }
        display.print("CANCEL");
    }
    prevBtnState_ = newState;
    return force;
}

ConfigurationMenu::ConfigurationMenu() : state_(State::MAIN_MENU), prevState_(State::MAIN_MENU)
{
}

bool ConfigurationMenu::loop(AdafruitOLED& display, const NavButton& btn, bool init)
{
    bool newState = (prevState_ != state_);
    if(init){
        state_ = State::MAIN_MENU;
        newState = true;
        display.clearDisplay();
        setText(display, "CONFIGURATION");
    }
    bool updateDisplay = false;
    display.setCursor(0, 0);
    updateDisplay |= render(display, newState);
    prevState_ = state_;
    switch(state_){
        case State::MAIN_MENU:
            updateDisplay |= mainMenu(state_, display, btn, newState);
            break;
        case State::IP_MENU:
            updateDisplay |= ipMenu(state_, display, btn, newState);
            break;
#ifdef HAS_AUTOCONFIG
        case State::AUTO_MENU:
            updateDisplay |= autoMenu(state_, display, btn, newState);
            break;
#endif
    }
    if(updateDisplay){
        display.display();
    }
    return state_ == State::EXIT;
}

bool ConfigurationMenu::mainMenu(State& newState, AdafruitOLED& display, const NavButton& btn, bool init)
{
    if(init){
        cfgList_.clearItems();
        cfgList_.addItem("IP configuration");
#ifdef HAS_AUTOCONFIG
        cfgList_.addItem("Auto configuration");
#endif
        cfgList_.addItem("Exit");
        setText(display, "CONFIGURATION");
    }
    int selected = -1;
    bool updated = cfgList_.loop(selected, display, btn);
    switch(selected){
        case 0:
            newState = State::IP_MENU;
            break;
#ifdef HAS_AUTOCONFIG
        case 1:
            newState = State::AUTO_MENU;
            break;
        case 2:
#else
        case 1:
#endif
            newState = State::EXIT;
            break;
    }
    return updated;
}

bool ConfigurationMenu::ipMenu(State& newState, AdafruitOLED& display, const NavButton& btn, bool init)
{
    enum class CFG_ITEM {
        NONE = 0, DHCP, IP_ADDR, SUBNET, GATEWAY
    };
    static CFG_ITEM cfgItem = CFG_ITEM::NONE;
        static bool useDHCP = false;
    static IPAddress addr, mask, gateway;
    if(init){
        configuration.getIPConfiguration(useDHCP, addr, mask, gateway);
        cfgList_.clearItems();
        cfgList_.addItem("DHCP");
        cfgList_.addItem("IP address");
        cfgList_.addItem("Subnet");
        cfgList_.addItem("Gateway");
        cfgList_.addItem("Save");
        cfgList_.addItem("Cancel");
        setText(display, "IP CONFIGURATION");
        cfgItem = CFG_ITEM::NONE;
    }
    bool changed = false;
    bool updateDisplay = false;
    if(cfgItem == CFG_ITEM::NONE){
        int selected = -1;
        updateDisplay |= cfgList_.loop(selected, display, btn);
        CFG_ITEM prevItem = cfgItem;
        switch(selected){
            case 0:
                cfgItem = CFG_ITEM::DHCP;
                break;
            case 1:
                cfgItem = CFG_ITEM::IP_ADDR;
                break;
            case 2:
                cfgItem = CFG_ITEM::SUBNET;
                break;
            case 3:
                cfgItem = CFG_ITEM::GATEWAY;
                break;
            case 4:
                //Save to configuration
                configuration.setIPConfiguration(useDHCP, addr, mask, gateway);
                newState = State::MAIN_MENU;
                return false;
            case 5:
                //Cancel, don't touch
                newState = State::MAIN_MENU;
                return false;
        }
        if(prevItem != cfgItem){
            changed = true;
        }
    }
    switch(cfgItem){
        case CFG_ITEM::DHCP:
        {
            if(changed)
                boolCfg_.setValue(useDHCP);
            int sel = -1;
            updateDisplay |= boolCfg_.loop(sel, display, btn, "Use DHCP?", "YES", "NO", changed);
            if(sel >= 0){
                cfgItem = CFG_ITEM::NONE;
                cfgList_.setDirty();
                if(sel == 1)
                    useDHCP = boolCfg_.getValue();
            }
        }
        break;
        case CFG_ITEM::IP_ADDR:
        {
            if(changed)
                ipCfg_.setIP(addr);
            int sel = -1;
            updateDisplay |= ipCfg_.loop(sel, display, btn, "Device IP", changed);
            if(sel >= 0){
                //Selected
                cfgItem = CFG_ITEM::NONE;
                cfgList_.setDirty();
                if(sel == 1)
                    addr = ipCfg_.getIP();
            }
        }
        break;
        case CFG_ITEM::SUBNET:
        {
            if(changed)
                ipCfg_.setIP(mask);
            int sel = -1;
            updateDisplay |= ipCfg_.loop(sel, display, btn, "Network mask", changed);
            if(sel >= 0){
                cfgItem = CFG_ITEM::NONE;
                cfgList_.setDirty();
                if(sel == 1)
                    mask = ipCfg_.getIP();
            }
        }
        break;
        case CFG_ITEM::GATEWAY:
        {
            if(changed)
                ipCfg_.setIP(gateway);
            int sel = -1;
            updateDisplay |= ipCfg_.loop(sel, display, btn, "Gateway", changed);
            if(sel >= 0){
                //Cancel
                cfgItem = CFG_ITEM::NONE;
                cfgList_.setDirty();
                if(sel == 1)
                    gateway = ipCfg_.getIP();
            }
        }
        break;
        default:
            cfgItem = CFG_ITEM::NONE;
    }
    return updateDisplay;
}

#ifdef HAS_AUTOCONFIG
bool ConfigurationMenu::autoMenu(State& newState, AdafruitOLED& display, const NavButton& btn, bool init)
{
    if(init){
        setText(display, "AUTO CFG");
        bool dhcp;
        IPAddress addr, mask, gw;
        configuration.getIPConfiguration(dhcp, addr, mask, gw);
        ipCfg_.setByte(addr[3]);
    }
    int sel = -1;
    bool updateDisplay = ipCfg_.loop(sel, display, btn, "IP suffix", init);
    if(sel >= 0){
        if(sel == 1){
            ESP_LOGI(TAG, "Applying auto configuration with suffix : %u", ipCfg_.getByte());
            AutoConfig::applyAutoconfig(ipCfg_.getByte());
        }
        newState = State::MAIN_MENU;
    }
    return updateDisplay;
}
#endif

#endif //NO_SCREEN

UI::UI() : state_{State::START},
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

void UI::gotIP()
{
    ipReceived_ = true;
}

void UI::loop()
{
    unsigned long lastChange = 0;
    while(true){
        unsigned long now = millis();
        navButton_.loop();
        NavButton::State state = navButton_.getState();
        btnState_ = state;
        switch(state_){
            case State::START:
                if(state == NavButton::State::IDLE){
                    //Button is IDLE check if reset combo is done
                    state_ = State::WAIT_RESET;
                    lastChange = now;
                }else{
                    state_ = State::RUNNING;
                }
                break;
            case State::WAIT_RESET:
                //Wait to see if user click button
                if((now - lastChange) > 1000){
                    if(state == NavButton::State::CLICKED){
                        state_ = State::CHECK_RESET;
#ifdef RGB_LED_PIN
                        userLed_.setResetSequence(1);
#endif
                        lastChange = now;
                    }else{
                        state_ = State::RUNNING;
                    }
                }
                break;
            case State::CHECK_RESET:
                if(state == NavButton::State::IDLE){
                    //User released the button
                    state_ = State::ASK_RESET;
#ifdef RGB_LED_PIN
                    userLed_.setResetSequence(2);
#endif
                    lastChange = now;
                }
                break;
            case State::ASK_RESET:
                if((now - lastChange) < 200){
                    if(state == NavButton::State::CLICKED){
                        //User released the button
                        state_ = State::VALIDATE_RESET;
#ifdef RGB_LED_PIN
                        userLed_.setResetSequence(3);
#endif
                        lastChange = now;
                    }
                }else{
                    state_ = State::RUNNING;
                }
                break;
            case State::VALIDATE_RESET:
                ESP_LOGI(TAG, "Reseting settings to default!");
                configuration.resetToDefault();
                if((now - lastChange) > 2000){
                    state_ = State::RUNNING;
                }
                break;
            case State::RUNNING:
                if(ipReceived_){
#ifdef RGB_LED_PIN
                    userLed_.endInitialization();
#endif
                    ipReceived_ = false;
                }
                break;
            }
#ifndef NO_SCREEN
        display_.loop(navButton_);
#ifdef RGB_LED_PIN
        userLed_.dim(!display_.isIdle());
#endif
#endif
        delay(10);
    }
}

UI ui;
