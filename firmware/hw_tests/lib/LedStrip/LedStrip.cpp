#include <LedStrip.hpp>
#include <Arduino.h>
#include "esp_log.h"

static const char* TAG = "LedStrip";
#define RMT_DEFAULT_RESOLUTION 10000000 // 10MHz resolution
#define RMT_MEMBLOCKS_DMA 1024          //Memory blocks in DMA mode
#define RMT_MEMBLOCKS_NO_DMA 64         //Memory blocks in normal mode
#define RMT_DEFAULT_TRANS_QUEUE_SIZE 4  //Tranfer queue size

LedStrip::EncoderMap LedStrip::encoders_;

void RGBPixel::setToBuffer(uint8_t* buffer) const
{
    *buffer = red_;
    *(buffer + 1) = green_;
    *(buffer + 2) = blue_;
}

void GRBPixel::setToBuffer(uint8_t* buffer) const
{
    *buffer = green_;
    *(buffer + 1) = red_;
    *(buffer + 2) = blue_;
}

LedStrip::LedStrip(int gpioNumber, int ledCount, double t0H, double t0L, double t1H, double t1L, bool useDMA) :
                    gpioNum_{gpioNumber}, ledCount_{ledCount}, t0H_{t0H}, t0L_{t0L}, t1H_{t1H}, t1L_{t1L}, useDMA_{useDMA},
                    scale_{100.0f}, rmtChan_{nullptr}, rtmEncoder_{nullptr}, rstEncoder_{nullptr},
                    buffer_{nullptr}, state_{ENCODE_STRIP}
{
    buffer_ = static_cast<uint8_t*>(calloc(1, sizeof(uint8_t) * 3));
    if(!buffer_){
        ESP_LOGE(TAG, "Unable to allocate LED strip buffer!");
    }
}

LedStrip::~LedStrip()
{
    if(buffer_){
        free(buffer_);
    }

    if(rmtChan_){
        rmt_del_channel(rmtChan_);
        rmtChan_ = nullptr;
    }

    destroyRMT();
}

void LedStrip::destroyRMT()
{
    if(stripEncoder_){
        stripEncoder_->del(stripEncoder_);
        delete stripEncoder_;
    }
}

void LedStrip::begin()
{
//Create the RMT channel
    esp_err_t ret = ESP_OK;
    uint32_t resolution = 10000000;
    rmt_clock_source_t clk_src = RMT_CLK_SRC_DEFAULT;
    size_t mem_block_symbols = useDMA_ ? RMT_MEMBLOCKS_DMA : RMT_MEMBLOCKS_NO_DMA;
    rmt_tx_channel_config_t rmt_chan_config = {
        .gpio_num = static_cast<gpio_num_t>(gpioNum_),
        .clk_src = clk_src,
        .resolution_hz = resolution,
        .mem_block_symbols = mem_block_symbols,
        .trans_queue_depth = RMT_DEFAULT_TRANS_QUEUE_SIZE,
        .flags = {
            .invert_out = false,
            .with_dma = useDMA_
        }
    };
    if(rmt_new_tx_channel(&rmt_chan_config, &rmtChan_) != ESP_OK){
        ESP_LOGE(TAG, "Unable to create RMT TX channel!");
        destroyRMT();
        return;
    }
    stripEncoder_ = new rmt_encoder_t{};
    stripEncoder_->encode = rmt_encode_led_strip;
    stripEncoder_->del = rmt_del_led_strip_encoder;
    stripEncoder_->reset = rmt_led_strip_encoder_reset;
    encoders_[stripEncoder_] = this;

    //Create the byte encoder for our led strip
    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .duration0 = static_cast<uint16_t>(t0H_ * resolution / 1000000), // T0H=0.3us
            .level0 = 1,
            .duration1 = static_cast<uint16_t>(t0L_ * resolution / 1000000), // T0L=0.9us
            .level1 = 0,
        },
        .bit1 = {
            .duration0 = static_cast<uint16_t>(t1H_ * resolution / 1000000), // T1H=0.9us
            .level0 = 1,
            .duration1 = static_cast<uint16_t>(t1L_ * resolution / 1000000), // T1L=0.3us
            .level1 = 0,
        },
        .flags = {
            .msb_first = 1 // WS2812 transfer bit order: G7...G0R7...R0B7...B0
        }
    };
    if(rmt_new_bytes_encoder(&bytes_encoder_config, &rtmEncoder_) != ESP_OK){
        ESP_LOGE(TAG, "create bytes encoder failed");
        destroyRMT();
        return;
    }

    //Create a copy encoder for reset
    rmt_copy_encoder_config_t copy_encoder_config = {};
    if(rmt_new_copy_encoder(&copy_encoder_config, &rstEncoder_) != ESP_OK){
        ESP_LOGE(TAG, "create bytes encoder failed");
        destroyRMT();
        return;
    }
    uint16_t reset_ticks = resolution / 1000000 * 50 / 2;
    rstCode_ = {
        .duration0 = reset_ticks,
        .level0 = 0,
        .duration1 = reset_ticks,
        .level1 = 0,
    };

    ESP_LOGI(TAG, "RMT created!");
}

void LedStrip::setLed(uint32_t ledNum, const LedPixel& pixel)
{
    if(ledNum < ledCount_){
        uint8_t* buf = buffer_ + 3*ledNum*sizeof(uint8_t);
        pixel.setToBuffer(buf);
        if(scale_ != 100.0f){
            for(int i=0;i<3;++i){
                buf[i] = static_cast<uint8_t>(static_cast<float>(buf[i]) * (scale_/100.0f));
            }
        }
        update();
    }
}

void LedStrip::update()
{
    rmt_transmit_config_t tx_conf = {
        .loop_count = 0,
    };
    if(rmt_enable(rmtChan_) != ESP_OK){
        ESP_LOGE(TAG, "Unable to enable RMT channel");
        return;
    }
    if(rmt_transmit(rmtChan_, stripEncoder_, buffer_, 3, &tx_conf) != ESP_OK){
        ESP_LOGE(TAG, "rmt_transmit failed");
        return;
    }
    if(rmt_tx_wait_all_done(rmtChan_, -1) != ESP_OK){
        ESP_LOGE(TAG, "rmt_tx_wait_all_done failed");
        return;
    }
    if(rmt_disable(rmtChan_) != ESP_OK){
        ESP_LOGE(TAG, "rmt_disable failed");
        return;
    }
}

RMT_ENCODER_FUNC_ATTR
size_t LedStrip::rmt_encode_led_strip(rmt_encoder_t *encoder, rmt_channel_handle_t channel, const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state)
{
    EncoderMap::iterator it = encoders_.find(encoder);
    if(it == encoders_.end()){
        ESP_LOGE(TAG, "Unable to get LED strip encoder instance!");
        return 0;
    }
    LedStrip* strip = it->second;
    rmt_encoder_handle_t bytes_encoder = strip->rtmEncoder_;
    rmt_encoder_handle_t copy_encoder = strip->rstEncoder_;
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    int state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    switch (strip->state_) {
    case ENCODE_STRIP: // send RGB data
        encoded_symbols += bytes_encoder->encode(bytes_encoder, channel, primary_data, data_size, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            strip->state_ = RESET_CODE; // switch to next state when current encoding session finished
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space for encoding artifacts
        }
    // fall-through
    case 1: // send reset code
        encoded_symbols += copy_encoder->encode(copy_encoder, channel, &strip->rstCode_,
                                                sizeof(strip->rstCode_), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            strip->state_ = ENCODE_STRIP; // back to the initial encoding session
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out; // yield if there's no free space for encoding artifacts
        }
    }
out:
    *ret_state = static_cast<rmt_encode_state_t>(state);
    return encoded_symbols;
}

esp_err_t LedStrip::rmt_del_led_strip_encoder(rmt_encoder_t *encoder)
{
    EncoderMap::iterator it = encoders_.find(encoder);
    if(it == encoders_.end()){
        ESP_LOGE(TAG, "Unable to get LED strip encoder instance!");
        return 0;
    }
    LedStrip* strip = it->second;
    if(strip->rtmEncoder_){
        rmt_del_encoder(strip->rtmEncoder_);
        strip->rtmEncoder_ = nullptr;
    }
    if(strip->rstEncoder_){
        rmt_del_encoder(strip->rstEncoder_);
        strip->rstEncoder_ = nullptr;
    }
    encoders_.erase(it);
    delete strip->stripEncoder_;
    strip->stripEncoder_ = nullptr;
    return ESP_OK;
}

RMT_ENCODER_FUNC_ATTR
esp_err_t LedStrip::rmt_led_strip_encoder_reset(rmt_encoder_t *encoder)
{
    EncoderMap::iterator it = encoders_.find(encoder);
    if(it == encoders_.end()){
        ESP_LOGE(TAG, "Unable to get LED strip encoder instance!");
        return 0;
    }
    LedStrip* strip = it->second;
    rmt_encoder_reset(strip->rtmEncoder_);
    rmt_encoder_reset(strip->rstEncoder_);
    strip->state_ = ENCODE_STRIP;
    return ESP_OK;
}

