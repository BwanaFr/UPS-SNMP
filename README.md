# USB UPS to SNMP

## Introduction

This small device is attended to publish USB UPS (tested on some APC models) values to SNMP (v2c).


## Building

### Hardware
Hardware design files (PCB and 3D enclosure) is stored in the `hardware` folder.

### Firmware
The ESP32S3 firmware is built with PlatformIO.
Releases of the firmware is available on my GitHub page.

## Usage
Connect an USB cable in the USB A connector, a PoE enabled RJ45 ethernet connection.
The device is starting using a default IP of 10.10.11.200.
Please navigate to http://10.10.11.200 to change settings (your PC needs to be on the same network).

### Reset to default using buttons procedure
You can do a "on site" reset using the navigation button. To do so, please use this procedure.

1. Connect ethernet cable
2. You have one second to push the button
3. The LED will go white
4. Relase the button
5. Within 200ms, press again the buton
6. Device will start with default configuration

