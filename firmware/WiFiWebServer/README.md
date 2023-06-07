# WiFi web server for BomberCat

Link: https://github.com/ElectronicCats/BomberCat/tree/wifiserver/firmware/WiFiWebServer

Author(s): Francisco Torres

Status: Draft

Last updated: 2023/06/7

## Contents

* Overview
* Developer guide

## Overview

This example shows how to use the WiFi module of the BomberCat to create a web server that can be accessed by any device connected to the same network.

## Developer guide

### Hardware required

* BomberCat
* USB-C cable

### Software required

Choose one of the following options:

* [Arduino IDE](https://www.arduino.cc/en/main/software)
* [arduino-cli](https://arduino.github.io/arduino-cli/latest/installation/)

### Libraries required

```
Used library           Version Path

SPI
WiFiNINA               1.8.14
Electronic Cats PN7150 1.8.0
Wire
```

### Platform required

```
Used platform              Version Path
electroniccats:mbed_rp2040 2.0.0
```

### Setup

1. Connect your BomberCat to your computer using the USB-C cable.
2. Open the `WiFiWebServer.ino` sketch using Arduino IDE or your favorite editor.
3. Select the `BomberCat` board from the Arduino IDE menu.
4. Select the port that corresponds to your board.
5. Compile and upload the sketch.

> Note: you don't need to worry about the web files, they are already included, just upload the `WiFiWebServer.ino` sketch.

### Run

1. Uncomment the line `#define DEBUG` in the `WiFiWebServer.ino` sketch. It's located at the beginning of the file.
```
#define DEBUG
```

2. Open the serial monitor.
3. Wait for the board to create the WiFi network. You should see something like this:
```
Creating access point named: BomberCat
SSID: BomberCat
Password: password
IP Address: http://192.168.4.1
Signal strength (RSSI): 0 dBm
```
4. Connect your device to the WiFi network using the password shown in the serial monitor.
5. Copy the IP address shown in the serial monitor.
6. Open a web browser and paste the IP address. You should see the web page served by the BomberCat.
