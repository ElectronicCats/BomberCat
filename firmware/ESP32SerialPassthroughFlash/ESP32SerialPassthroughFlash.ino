/*
   ESP32SerialPassthroughFlash v1.0
   ESP32 / NINA Serial Passthrough for flashing using Arduino MCU
   This Arduino sketch allows you to send AT commands, watch bootloader messages, or flash an ESP32 through a RP2040 or similar MCU. 
   The MCU should be operating at 3.3V, if not, voltage level shifters must be used on the connections to ESP.
   The program might also work on other ESPs, but have only been tested with ESP32.
   
   Developed and tested with a Arduino RP2040-based.
   The program might work on MCUs without native USB, but no guarantees! :-)
   Sending AT commands has been tested using the Arduino serial monitor, Visual Micro serial monitor and Putty in serial mode.
   The same goes for watching bootloader output.
   Firmware upgrade/flashing has been done with the official ESP32 Flash Download Tool from https://www.espressif.com/en/support/download/other-tools, 
   with firmware downloaded from https://www.espressif.com/en/support/download/at
  Copyright 2021 Kåre Smith (Kaare Smith)
  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

// The pin used on the Arduino board. Important: must be 3.3V GPIO pins, or use voltage level shifter
#define GPIO0_PIN 20  // Arduino pin connected to GPIO0 pin on ESP32
#define RESET_PIN 24 // Arduino pin connected to RESET pin on ESP32

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(GPIO0_PIN, OUTPUT);
  pinMode(RESET_PIN, OUTPUT);
  delay(500);

  enterFlashMode();
}

void loop() {
  if (Serial.available()) {      // If anything comes in Serial (USB),
    Serial2.write(Serial.read());   // read it and send it out Serial1 (pins 0 & 1)
  }

  if (Serial2.available()) {     // If anything comes in Serial1 (pins 0 & 1)
    Serial.write(Serial2.read());   // read it and send it out Serial (USB)
  }
  
}

// Enter flashing mode
void enterFlashMode() {
  digitalWrite(NINA_GPIO0, LOW);

  digitalWrite(NINA_RESETN, LOW);
  delay(100);
  digitalWrite(NINA_RESETN, HIGH);
  delay(100);
  digitalWrite(NINA_RESETN, LOW);

  // Now the ESP32 should be in flashing mode
}
