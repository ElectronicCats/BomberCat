#!/bin/sh

./compile.sh
arduino-cli upload -p /dev/cu.usbmodem11101 --fqbn electroniccats:mbed_rp2040:bombercat WiFiWebServer.ino