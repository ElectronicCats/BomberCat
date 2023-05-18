# WiFi web server for BomberCat

Link: https://github.com/ElectronicCats/BomberCat/tree/wifiserver/firmware/WiFiWebServer

Author(s): Francisco Torres

Status: Draft

Last updated: 2023/05/18

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

### Setup
1. Modify the `ssid` and `password` variables in the `arduino_secrets.h` file to match your network credentials.

```
#define SECRET_SSID "deimos" // Name of your WiFi network
#define SECRET_PASS "hello123" // Password of your WiFi network
```

1. Connect your BomberCat to your computer using the USB-C cable.
2. Open the `WiFiWebServer.ino` sketch using Arduino IDE or your favorite editor.
3. Select the `BomberCat` board from the Arduino IDE menu.
4. Select the port that corresponds to your board.
5. Compile and upload the sketch.

> Note: you don't need to worry about the web files, they are already included, just upload the `WiFiWebServer.ino` sketch.

### Run

1. Open the serial monitor.
2. Wait for the board to connect to the network.
3. Copy the IP address shown in the serial monitor.
4. Open a web browser and paste the IP address.
5. You should see the web page served by the BomberCat.

> Note: the BomberCat and your device must be connected to the same network.