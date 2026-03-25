#ifndef _LED_STRIP_HPP__
#define _LED_STRIP_HPP__

#include <stdint.h>
#include "driver/rmt_tx.h"
#include "esp_idf_version.h"
#include <map>

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 5, 0)
#if CONFIG_RMT_ISR_IRAM_SAFE
#define RMT_ENCODER_FUNC_ATTR IRAM_ATTR
#else
#define RMT_ENCODER_FUNC_ATTR
#endif // CONFIG_RMT_ISR_IRAM_SAFE
#endif // ESP_IDF_VERSION

/**
 * For setting a led value
 */
class LedPixel{
public:
    constexpr LedPixel(const uint8_t r, const uint8_t g, const uint8_t b) : red_{r}, green_{g}, blue_{b}{};
    virtual ~LedPixel() = default;

protected:
    friend class LedStrip;
    virtual void setToBuffer(uint8_t* buffer) const = 0;
    uint8_t red_;
    uint8_t green_;
    uint8_t blue_;
};

/**
 * RGB pixels as found on WS2811
 */
class RGBPixel : public LedPixel{
public:
    constexpr RGBPixel(const uint8_t r, const uint8_t g, const uint8_t b) : LedPixel(r,g,b){};
    virtual ~RGBPixel() = default;
    inline RGBPixel& operator= (const uint32_t colorcode)
    {
        red_ = (colorcode >> 16) & 0xFF;
        green_ = (colorcode >>  8) & 0xFF;
        blue_ = (colorcode >>  0) & 0xFF;
        return *this;
    };
protected:
    void setToBuffer(uint8_t* buffer) const;
};

/**
 * GRBPixel as found on WS2812
 */
class GRBPixel : public LedPixel{
public:
    constexpr GRBPixel(const uint8_t r, const uint8_t g, const uint8_t b) : LedPixel(r,g,b){};
    virtual ~GRBPixel() = default;
    inline GRBPixel& operator= (const uint32_t colorcode)
    {
        red_ = (colorcode >> 16) & 0xFF;
        green_ = (colorcode >>  8) & 0xFF;
        blue_ = (colorcode >>  0) & 0xFF;
        return *this;
    };
protected:
    void setToBuffer(uint8_t* buffer) const;
};

class LedStrip{
public:
    /**
     * Constructor
     * @param gpioNumber GPIO number for data line
     * @param ledCount Number of leds
     * @param t0H Time high, 0 code
     * @param t0L Time low, 0 code
     * @param t1H Time high, 1 code
     * @param t1L Time low, 1 code
     * @param useDMA True to use DMA transfer (ESP32-S3/P4)
     */
    LedStrip(int gpioNumber, int ledCount=1, double t0H=0.35, double t0L=0.8, double t1H=0.7, double t1L=0.6, bool useDMA=false);
    virtual ~LedStrip();

    void begin();

    /**
     * Sets led
     * @param ledNum Number of the led to be set
     * @param pixel Pixel value
     */
    void setLed(uint32_t ledNum, const LedPixel& pixel);

    /**
     * Sets LED scale
     */
    inline void setScale(const float scale) { scale_ = scale; }

/// Predefined RGB colors
    enum class ColorCode {
        AliceBlue=0xF0F8FF,             ///< @htmlcolorblock{F0F8FF}
        Amethyst=0x9966CC,              ///< @htmlcolorblock{9966CC}
        AntiqueWhite=0xFAEBD7,          ///< @htmlcolorblock{FAEBD7}
        Aqua=0x00FFFF,                  ///< @htmlcolorblock{00FFFF}
        Aquamarine=0x7FFFD4,            ///< @htmlcolorblock{7FFFD4}
        Azure=0xF0FFFF,                 ///< @htmlcolorblock{F0FFFF}
        Beige=0xF5F5DC,                 ///< @htmlcolorblock{F5F5DC}
        Bisque=0xFFE4C4,                ///< @htmlcolorblock{FFE4C4}
        Black=0x000000,                 ///< @htmlcolorblock{000000}
        BlanchedAlmond=0xFFEBCD,        ///< @htmlcolorblock{FFEBCD}
        Blue=0x0000FF,                  ///< @htmlcolorblock{0000FF}
        BlueViolet=0x8A2BE2,            ///< @htmlcolorblock{8A2BE2}
        Brown=0xA52A2A,                 ///< @htmlcolorblock{A52A2A}
        BurlyWood=0xDEB887,             ///< @htmlcolorblock{DEB887}
        CadetBlue=0x5F9EA0,             ///< @htmlcolorblock{5F9EA0}
        Chartreuse=0x7FFF00,            ///< @htmlcolorblock{7FFF00}
        Chocolate=0xD2691E,             ///< @htmlcolorblock{D2691E}
        Coral=0xFF7F50,                 ///< @htmlcolorblock{FF7F50}
        CornflowerBlue=0x6495ED,        ///< @htmlcolorblock{6495ED}
        Cornsilk=0xFFF8DC,              ///< @htmlcolorblock{FFF8DC}
        Crimson=0xDC143C,               ///< @htmlcolorblock{DC143C}
        Cyan=0x00FFFF,                  ///< @htmlcolorblock{00FFFF}
        DarkBlue=0x00008B,              ///< @htmlcolorblock{00008B}
        DarkCyan=0x008B8B,              ///< @htmlcolorblock{008B8B}
        DarkGoldenrod=0xB8860B,         ///< @htmlcolorblock{B8860B}
        DarkGray=0xA9A9A9,              ///< @htmlcolorblock{A9A9A9}
        DarkGrey=0xA9A9A9,              ///< @htmlcolorblock{A9A9A9}
        DarkGreen=0x006400,             ///< @htmlcolorblock{006400}
        DarkKhaki=0xBDB76B,             ///< @htmlcolorblock{BDB76B}
        DarkMagenta=0x8B008B,           ///< @htmlcolorblock{8B008B}
        DarkOliveGreen=0x556B2F,        ///< @htmlcolorblock{556B2F}
        DarkOrange=0xFF8C00,            ///< @htmlcolorblock{FF8C00}
        DarkOrchid=0x9932CC,            ///< @htmlcolorblock{9932CC}
        DarkRed=0x8B0000,               ///< @htmlcolorblock{8B0000}
        DarkSalmon=0xE9967A,            ///< @htmlcolorblock{E9967A}
        DarkSeaGreen=0x8FBC8F,          ///< @htmlcolorblock{8FBC8F}
        DarkSlateBlue=0x483D8B,         ///< @htmlcolorblock{483D8B}
        DarkSlateGray=0x2F4F4F,         ///< @htmlcolorblock{2F4F4F}
        DarkSlateGrey=0x2F4F4F,         ///< @htmlcolorblock{2F4F4F}
        DarkTurquoise=0x00CED1,         ///< @htmlcolorblock{00CED1}
        DarkViolet=0x9400D3,            ///< @htmlcolorblock{9400D3}
        DeepPink=0xFF1493,              ///< @htmlcolorblock{FF1493}
        DeepSkyBlue=0x00BFFF,           ///< @htmlcolorblock{00BFFF}
        DimGray=0x696969,               ///< @htmlcolorblock{696969}
        DimGrey=0x696969,               ///< @htmlcolorblock{696969}
        DodgerBlue=0x1E90FF,            ///< @htmlcolorblock{1E90FF}
        FireBrick=0xB22222,             ///< @htmlcolorblock{B22222}
        FloralWhite=0xFFFAF0,           ///< @htmlcolorblock{FFFAF0}
        ForestGreen=0x228B22,           ///< @htmlcolorblock{228B22}
        Fuchsia=0xFF00FF,               ///< @htmlcolorblock{FF00FF}
        Gainsboro=0xDCDCDC,             ///< @htmlcolorblock{DCDCDC}
        GhostWhite=0xF8F8FF,            ///< @htmlcolorblock{F8F8FF}
        Gold=0xFFD700,                  ///< @htmlcolorblock{FFD700}
        Goldenrod=0xDAA520,             ///< @htmlcolorblock{DAA520}
        Gray=0x808080,                  ///< @htmlcolorblock{808080}
        Grey=0x808080,                  ///< @htmlcolorblock{808080}
        Green=0x008000,                 ///< @htmlcolorblock{008000}
        GreenYellow=0xADFF2F,           ///< @htmlcolorblock{ADFF2F}
        Honeydew=0xF0FFF0,              ///< @htmlcolorblock{F0FFF0}
        HotPink=0xFF69B4,               ///< @htmlcolorblock{FF69B4}
        IndianRed=0xCD5C5C,             ///< @htmlcolorblock{CD5C5C}
        Indigo=0x4B0082,                ///< @htmlcolorblock{4B0082}
        Ivory=0xFFFFF0,                 ///< @htmlcolorblock{FFFFF0}
        Khaki=0xF0E68C,                 ///< @htmlcolorblock{F0E68C}
        Lavender=0xE6E6FA,              ///< @htmlcolorblock{E6E6FA}
        LavenderBlush=0xFFF0F5,         ///< @htmlcolorblock{FFF0F5}
        LawnGreen=0x7CFC00,             ///< @htmlcolorblock{7CFC00}
        LemonChiffon=0xFFFACD,          ///< @htmlcolorblock{FFFACD}
        LightBlue=0xADD8E6,             ///< @htmlcolorblock{ADD8E6}
        LightCoral=0xF08080,            ///< @htmlcolorblock{F08080}
        LightCyan=0xE0FFFF,             ///< @htmlcolorblock{E0FFFF}
        LightGoldenrodYellow=0xFAFAD2,  ///< @htmlcolorblock{FAFAD2}
        LightGreen=0x90EE90,            ///< @htmlcolorblock{90EE90}
        LightGrey=0xD3D3D3,             ///< @htmlcolorblock{D3D3D3}
        LightPink=0xFFB6C1,             ///< @htmlcolorblock{FFB6C1}
        LightSalmon=0xFFA07A,           ///< @htmlcolorblock{FFA07A}
        LightSeaGreen=0x20B2AA,         ///< @htmlcolorblock{20B2AA}
        LightSkyBlue=0x87CEFA,          ///< @htmlcolorblock{87CEFA}
        LightSlateGray=0x778899,        ///< @htmlcolorblock{778899}
        LightSlateGrey=0x778899,        ///< @htmlcolorblock{778899}
        LightSteelBlue=0xB0C4DE,        ///< @htmlcolorblock{B0C4DE}
        LightYellow=0xFFFFE0,           ///< @htmlcolorblock{FFFFE0}
        Lime=0x00FF00,                  ///< @htmlcolorblock{00FF00}
        LimeGreen=0x32CD32,             ///< @htmlcolorblock{32CD32}
        Linen=0xFAF0E6,                 ///< @htmlcolorblock{FAF0E6}
        Magenta=0xFF00FF,               ///< @htmlcolorblock{FF00FF}
        Maroon=0x800000,                ///< @htmlcolorblock{800000}
        MediumAquamarine=0x66CDAA,      ///< @htmlcolorblock{66CDAA}
        MediumBlue=0x0000CD,            ///< @htmlcolorblock{0000CD}
        MediumOrchid=0xBA55D3,          ///< @htmlcolorblock{BA55D3}
        MediumPurple=0x9370DB,          ///< @htmlcolorblock{9370DB}
        MediumSeaGreen=0x3CB371,        ///< @htmlcolorblock{3CB371}
        MediumSlateBlue=0x7B68EE,       ///< @htmlcolorblock{7B68EE}
        MediumSpringGreen=0x00FA9A,     ///< @htmlcolorblock{00FA9A}
        MediumTurquoise=0x48D1CC,       ///< @htmlcolorblock{48D1CC}
        MediumVioletRed=0xC71585,       ///< @htmlcolorblock{C71585}
        MidnightBlue=0x191970,          ///< @htmlcolorblock{191970}
        MintCream=0xF5FFFA,             ///< @htmlcolorblock{F5FFFA}
        MistyRose=0xFFE4E1,             ///< @htmlcolorblock{FFE4E1}
        Moccasin=0xFFE4B5,              ///< @htmlcolorblock{FFE4B5}
        NavajoWhite=0xFFDEAD,           ///< @htmlcolorblock{FFDEAD}
        Navy=0x000080,                  ///< @htmlcolorblock{000080}
        OldLace=0xFDF5E6,               ///< @htmlcolorblock{FDF5E6}
        Olive=0x808000,                 ///< @htmlcolorblock{808000}
        OliveDrab=0x6B8E23,             ///< @htmlcolorblock{6B8E23}
        Orange=0xFFA500,                ///< @htmlcolorblock{FFA500}
        OrangeRed=0xFF4500,             ///< @htmlcolorblock{FF4500}
        Orchid=0xDA70D6,                ///< @htmlcolorblock{DA70D6}
        PaleGoldenrod=0xEEE8AA,         ///< @htmlcolorblock{EEE8AA}
        PaleGreen=0x98FB98,             ///< @htmlcolorblock{98FB98}
        PaleTurquoise=0xAFEEEE,         ///< @htmlcolorblock{AFEEEE}
        PaleVioletRed=0xDB7093,         ///< @htmlcolorblock{DB7093}
        PapayaWhip=0xFFEFD5,            ///< @htmlcolorblock{FFEFD5}
        PeachPuff=0xFFDAB9,             ///< @htmlcolorblock{FFDAB9}
        Peru=0xCD853F,                  ///< @htmlcolorblock{CD853F}
        Pink=0xFFC0CB,                  ///< @htmlcolorblock{FFC0CB}
        Plaid=0xCC5533,                 ///< @htmlcolorblock{CC5533}
        Plum=0xDDA0DD,                  ///< @htmlcolorblock{DDA0DD}
        PowderBlue=0xB0E0E6,            ///< @htmlcolorblock{B0E0E6}
        Purple=0x800080,                ///< @htmlcolorblock{800080}
        Red=0xFF0000,                   ///< @htmlcolorblock{FF0000}
        RosyBrown=0xBC8F8F,             ///< @htmlcolorblock{BC8F8F}
        RoyalBlue=0x4169E1,             ///< @htmlcolorblock{4169E1}
        SaddleBrown=0x8B4513,           ///< @htmlcolorblock{8B4513}
        Salmon=0xFA8072,                ///< @htmlcolorblock{FA8072}
        SandyBrown=0xF4A460,            ///< @htmlcolorblock{F4A460}
        SeaGreen=0x2E8B57,              ///< @htmlcolorblock{2E8B57}
        Seashell=0xFFF5EE,              ///< @htmlcolorblock{FFF5EE}
        Sienna=0xA0522D,                ///< @htmlcolorblock{A0522D}
        Silver=0xC0C0C0,                ///< @htmlcolorblock{C0C0C0}
        SkyBlue=0x87CEEB,               ///< @htmlcolorblock{87CEEB}
        SlateBlue=0x6A5ACD,             ///< @htmlcolorblock{6A5ACD}
        SlateGray=0x708090,             ///< @htmlcolorblock{708090}
        SlateGrey=0x708090,             ///< @htmlcolorblock{708090}
        Snow=0xFFFAFA,                  ///< @htmlcolorblock{FFFAFA}
        SpringGreen=0x00FF7F,           ///< @htmlcolorblock{00FF7F}
        SteelBlue=0x4682B4,             ///< @htmlcolorblock{4682B4}
        Tan=0xD2B48C,                   ///< @htmlcolorblock{D2B48C}
        Teal=0x008080,                  ///< @htmlcolorblock{008080}
        Thistle=0xD8BFD8,               ///< @htmlcolorblock{D8BFD8}
        Tomato=0xFF6347,                ///< @htmlcolorblock{FF6347}
        Turquoise=0x40E0D0,             ///< @htmlcolorblock{40E0D0}
        Violet=0xEE82EE,                ///< @htmlcolorblock{EE82EE}
        Wheat=0xF5DEB3,                 ///< @htmlcolorblock{F5DEB3}
        White=0xFFFFFF,                 ///< @htmlcolorblock{FFFFFF}
        WhiteSmoke=0xF5F5F5,            ///< @htmlcolorblock{F5F5F5}
        Yellow=0xFFFF00,                ///< @htmlcolorblock{FFFF00}
        YellowGreen=0x9ACD32,           ///< @htmlcolorblock{9ACD32}

        // LED RGB color that roughly approximates
        // the color of incandescent fairy lights,
        // assuming that you're using FastLED
        // color correction on your LEDs (recommended).
        FairyLight=0xFFE42D,           ///< @htmlcolorblock{FFE42D}

        // If you are using no color correction, use this
        FairyLightNCC=0xFF9D2A,         ///< @htmlcolorblock{FFE42D}

        // TCL Color Extensions - Essential additions for LED programming
        // These colors provide useful grayscale levels and color variants

        // Essential grayscale levels (0-100 scale for precise dimming)
        Gray0=0x000000,                ///< TCL grayscale 0% @htmlcolorblock{000000}
        Gray10=0x1A1A1A,               ///< TCL grayscale 10% @htmlcolorblock{1A1A1A}
        Gray25=0x404040,               ///< TCL grayscale 25% @htmlcolorblock{404040}
        Gray50=0x7F7F7F,               ///< TCL grayscale 50% @htmlcolorblock{7F7F7F}
        Gray75=0xBFBFBF,               ///< TCL grayscale 75% @htmlcolorblock{BFBFBF}
        Gray100=0xFFFFFF,              ///< TCL grayscale 100% @htmlcolorblock{FFFFFF}

        // Alternative grey spellings
        Grey0=0x000000,                ///< TCL grayscale 0% @htmlcolorblock{000000}
        Grey10=0x1A1A1A,               ///< TCL grayscale 10% @htmlcolorblock{1A1A1A}
        Grey25=0x404040,               ///< TCL grayscale 25% @htmlcolorblock{404040}
        Grey50=0x7F7F7F,               ///< TCL grayscale 50% @htmlcolorblock{7F7F7F}
        Grey75=0xBFBFBF,               ///< TCL grayscale 75% @htmlcolorblock{BFBFBF}
        Grey100=0xFFFFFF,              ///< TCL grayscale 100% @htmlcolorblock{FFFFFF}

        // Primary color variants (1-4 intensity levels)
        Red1=0xFF0000,                 ///< TCL red variant 1 (brightest) @htmlcolorblock{FF0000}
        Red2=0xEE0000,                 ///< TCL red variant 2 @htmlcolorblock{EE0000}
        Red3=0xCD0000,                 ///< TCL red variant 3 @htmlcolorblock{CD0000}
        Red4=0x8B0000,                 ///< TCL red variant 4 (darkest) @htmlcolorblock{8B0000}

        Green1=0x00FF00,               ///< TCL green variant 1 (brightest) @htmlcolorblock{00FF00}
        Green2=0x00EE00,               ///< TCL green variant 2 @htmlcolorblock{00EE00}
        Green3=0x00CD00,               ///< TCL green variant 3 @htmlcolorblock{00CD00}
        Green4=0x008B00,               ///< TCL green variant 4 (darkest) @htmlcolorblock{008B00}

        Blue1=0x0000FF,                ///< TCL blue variant 1 (brightest) @htmlcolorblock{0000FF}
        Blue2=0x0000EE,                ///< TCL blue variant 2 @htmlcolorblock{0000EE}
        Blue3=0x0000CD,                ///< TCL blue variant 3 @htmlcolorblock{0000CD}
        Blue4=0x00008B,                ///< TCL blue variant 4 (darkest) @htmlcolorblock{00008B}

        // Useful warm color variants for LED ambience
        Orange1=0xFFA500,              ///< TCL orange variant 1 @htmlcolorblock{FFA500}
        Orange2=0xEE9A00,              ///< TCL orange variant 2 @htmlcolorblock{EE9A00}
        Orange3=0xCD8500,              ///< TCL orange variant 3 @htmlcolorblock{CD8500}
        Orange4=0x8B5A00,              ///< TCL orange variant 4 @htmlcolorblock{8B5A00}

        Yellow1=0xFFFF00,              ///< TCL yellow variant 1 @htmlcolorblock{FFFF00}
        Yellow2=0xEEEE00,              ///< TCL yellow variant 2 @htmlcolorblock{EEEE00}
        Yellow3=0xCDCD00,              ///< TCL yellow variant 3 @htmlcolorblock{CDCD00}
        Yellow4=0x8B8B00,              ///< TCL yellow variant 4 @htmlcolorblock{8B8B00}

        // Popular LED colors for effects
        Cyan1=0x00FFFF,                ///< TCL cyan variant 1 @htmlcolorblock{00FFFF}
        Cyan2=0x00EEEE,                ///< TCL cyan variant 2 @htmlcolorblock{00EEEE}
        Cyan3=0x00CDCD,                ///< TCL cyan variant 3 @htmlcolorblock{00CDCD}
        Cyan4=0x008B8B,                ///< TCL cyan variant 4 @htmlcolorblock{008B8B}

        Magenta1=0xFF00FF,             ///< TCL magenta variant 1 @htmlcolorblock{FF00FF}
        Magenta2=0xEE00EE,             ///< TCL magenta variant 2 @htmlcolorblock{EE00EE}
        Magenta3=0xCD00CD,             ///< TCL magenta variant 3 @htmlcolorblock{CD00CD}
        Magenta4=0x8B008B,             ///< TCL magenta variant 4 @htmlcolorblock{8B008B}

        // Additional useful colors for LED programming
        VioletRed=0xD02090,            ///< TCL violet red @htmlcolorblock{D02090}
        DeepPink1=0xFF1493,            ///< TCL deep pink variant 1 @htmlcolorblock{FF1493}
        DeepPink2=0xEE1289,            ///< TCL deep pink variant 2 @htmlcolorblock{EE1289}
        DeepPink3=0xCD1076,            ///< TCL deep pink variant 3 @htmlcolorblock{CD1076}
        DeepPink4=0x8B0A50,            ///< TCL deep pink variant 4 @htmlcolorblock{8B0A50}

        Gold1=0xFFD700,                ///< TCL gold variant 1 @htmlcolorblock{FFD700}
        Gold2=0xEEC900,                ///< TCL gold variant 2 @htmlcolorblock{EEC900}
        Gold3=0xCDAD00,                ///< TCL gold variant 3 @htmlcolorblock{CDAD00}
        Gold4=0x8B7500                 ///< TCL gold variant 4 @htmlcolorblock{8B7500}

    };

private:
    const int gpioNum_;                 //!< GPIO number
    const int ledCount_;                //!< Number of leds
    const double t0H_;                  //!< Time high, 0 code
    const double t0L_;                  //!< Time low, 0 code
    const double t1H_;                  //!< Time high, 1 code
    const double t1L_;                  //!< Time low, 1 code
    const bool useDMA_;                 //!< Use DMA flag
    float scale_;                       //!< Scale in percent
    rmt_channel_handle_t rmtChan_;      //!< Handle to RTM channel
    rmt_encoder_t* stripEncoder_;       //!< LED strip encoder
    rmt_encoder_t* rtmEncoder_;         //!< Strip bytes encoder
    rmt_encoder_t* rstEncoder_;         //!< Reset copy encoder
    rmt_symbol_word_t rstCode_;         //!< Reset code
    uint8_t* buffer_;                   //!< Buffer
    enum State{
        ENCODE_STRIP = 0,
        RESET_CODE
    };
    State state_;                       //!< Encoder state

    /**
     * Destroy RMT ressources
     */
    void destroyRMT();

    /**
     * Updates LEDs
     */
    void update();

    RMT_ENCODER_FUNC_ATTR
    static size_t rmt_encode_led_strip(rmt_encoder_t *encoder, rmt_channel_handle_t channel, const void *primary_data, size_t data_size, rmt_encode_state_t *ret_state);
    static esp_err_t rmt_del_led_strip_encoder(rmt_encoder_t *encoder);
    RMT_ENCODER_FUNC_ATTR
    static esp_err_t rmt_led_strip_encoder_reset(rmt_encoder_t *encoder);

    typedef std::map<rmt_encoder_t*, LedStrip*> EncoderMap;
    static EncoderMap encoders_;
};




#endif //_LED_STRIP_HPP__
