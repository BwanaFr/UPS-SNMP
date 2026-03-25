#ifndef _DEVICE_PINS_HPP__
#define _DEVICE_PINS_HPP__

//Pins defs for W5500 chip
#define ETH_MOSI_PIN 11
#define ETH_MISO_PIN 12
#define ETH_SCLK_PIN 13
#define ETH_CS_PIN 14
#define ETH_INT_PIN 10
#define ETH_RST_PIN 9

//OLED I2C pins
#ifndef SDA_OLED_PIN
#define SDA_OLED_PIN 16
#endif

#ifndef SCL_OLED_PIN
#define SCL_OLED_PIN 17
#endif

#endif