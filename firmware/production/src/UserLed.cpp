/*
**    This software license is not yet defined.
**
*/
#ifdef RGB_LED_PIN
#include "UserLed.hpp"

UserLed* UserLed::instance_ = nullptr;

UserLed::UserLed() : state_(State::INITIALIZING),
            ledStrip_(RGB_LED_PIN, 1),
            ledColor_(GRBPixel{0,0,0}),
            customColor_(GRBPixel{0,0,0}),
            isOn_(false), isDim_(false), waitTicks_(portMAX_DELAY),
            semaphore_(xSemaphoreCreateMutex()),
            newStateQueue_(xQueueCreate(10, sizeof(NewState)))
{
    instance_ = this;
    StatusProvider::registerAlarmListener(this);
}

UserLed::~UserLed()
{
    if(semaphore_){
        vSemaphoreDelete(semaphore_);
        semaphore_ = nullptr;
    }
}

void UserLed::begin(){
    ledStrip_.begin();
    xTaskCreatePinnedToCore(
        ledTask,
        "ledTask",
        2048,
        (void*)this,
        tskIDLE_PRIORITY + 1, NULL,
        ESP_TASK_MAIN_CORE ? 0 : 1
    );
}

void UserLed::ledTask(void* param)
{
    UserLed* tsk = static_cast<UserLed*>(param);
    while(true){
        tsk->loop();
    }
}

void UserLed::loop(){
    NewState newState;
    if(xQueueReceive( newStateQueue_, &newState, waitTicks_) == pdPASS ){
        if(newState.state == State::DIM){
            if(!isDim_){
                ledStrip_.setScale(DIM_SCALE);
                ledStrip_.setLed(0, ledColor_);
                isDim_ = true;
                return;
            }
        }else if(newState.state == State::UNDIM){
            if(isDim_){
                ledStrip_.setScale(100.0);
                ledStrip_.setLed(0, ledColor_);
                isDim_ = false;
                return;
            }
        }else if(newState.state == State::CUSTOM){
            ledColor_ = newState.payload;
            ledStrip_.setLed(0, ledColor_);
            waitTicks_ = portMAX_DELAY;
        }else{
            isOn_ = false;
            waitTicks_ = pdMS_TO_TICKS(STATE_SETTINGS[static_cast<int32_t>(newState.state)].timeOn);
        }
        xSemaphoreTake(semaphore_, portMAX_DELAY);
        state_ = newState.state;
        xSemaphoreGive(semaphore_);
    }
    if(state_ != State::CUSTOM){
        isOn_ = !isOn_;
        waitTicks_ = pdMS_TO_TICKS(isOn_ ? STATE_SETTINGS[static_cast<int32_t>(state_)].timeOn : STATE_SETTINGS[static_cast<int32_t>(state_)].timeOff);
        ledColor_ = static_cast<uint32_t>(isOn_ ? STATE_SETTINGS[static_cast<int32_t>(state_)].onColor : STATE_SETTINGS[static_cast<int32_t>(state_)].offColor);
        ledStrip_.setLed(0, ledColor_);
    }
}

void UserLed::fatal()
{
    //Don't send if already in fatal
    setNewState(State::FATAL);
}

void UserLed::endInitialization()
{
    AlarmSeverity alarmSev = StatusProvider::getMostAlarmSeverity();
    switch(alarmSev){
        case AlarmSeverity::INACTIVE:
            setNewState(State::OK);
            break;
        case AlarmSeverity::WARNING:
            setNewState(State::WARNING);
            break;
        case AlarmSeverity::ERROR:
            setNewState(State::ERROR);
            break;
    }
}

void UserLed::setNewState(State newState, uint32_t payload)
{
    if((newState == State::CUSTOM) || (newState != state_)){
        NewState state = {newState, payload};
        xQueueSend( /* The handle of the queue. */
                newStateQueue_,
                ( void * ) &state,
                ( TickType_t ) 0 );
    }
}

UserLed::State UserLed::getState()
{
    State ret;
    xSemaphoreTake(semaphore_, portMAX_DELAY);
    ret = state_;
    xSemaphoreGive(semaphore_);
    return ret;
}

void UserLed::customColor(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t customColor = (r<<16) | (g<<8) | b;
    setNewState(State::CUSTOM, (uint32_t)customColor);
}

void UserLed::customHSV(float hue, float saturation, float value)
{
    uint8_t r,g,b;
    HSVToRGB(hue, saturation, value, r, g, b);
    customColor(r, g, b);
}

void UserLed::HSVToRGB(float hue, float saturation, float value, uint8_t& red, uint8_t& green, uint8_t& blue)
{
    float r = 0, g = 0, b = 0;
	if (saturation == 0)
	{
		r = value;
		g = value;
		b = value;
	}
	else
	{
		int i;
		double f, p, q, t;

		if (hue == 360)
			hue = 0;
		else
			hue = hue / 60;

		i = (int)trunc(hue);
		f = hue - i;

		p = value * (1.0 - saturation);
		q = value * (1.0 - (saturation * f));
		t = value * (1.0 - (saturation * (1.0 - f)));

		switch (i)
		{
		case 0:
			r = value;
			g = t;
			b = p;
			break;

		case 1:
			r = q;
			g = value;
			b = p;
			break;

		case 2:
			r = p;
			g = value;
			b = t;
			break;

		case 3:
			r = p;
			g = q;
			b = value;
			break;

		case 4:
			r = t;
			g = p;
			b = value;
			break;

		default:
			r = value;
			g = p;
			b = q;
			break;
		}
	}

	red = (uint8_t)(r * 255);
	green = (uint8_t)(g * 255);
	blue = (uint8_t)(b * 255);
}

void UserLed::alarmChanged(const AlarmInfo& info)
{
    if((state_ == State::OK)  || (state_ == State::ERROR) || (state_ == State::WARNING)){
        //Not fatal or initalizing, we can use alarms delivered by StatusProviders
        AlarmSeverity severity = info.severity;
        if(severity == AlarmSeverity::ERROR){
            setNewState(State::ERROR);
        }else if(severity == AlarmSeverity::WARNING){
            setNewState(State::WARNING);
        }else if(severity == AlarmSeverity::INACTIVE){
            endInitialization();
        }
    }
}

void UserLed::setResetSequence(int seqNum)
{
    switch(seqNum){
        case 1:
            setNewState(State::INIT_RESET_1);
            break;
        case 2:
            setNewState(State::INIT_RESET_2);
            break;
        case 3:
            setNewState(State::INIT_RESET_3);
            break;
    }
}

void UserLed::dim(bool dim)
{
    if(dim != isDim_){
        setNewState(dim ? State::DIM : State::UNDIM);
    }
}

#endif
