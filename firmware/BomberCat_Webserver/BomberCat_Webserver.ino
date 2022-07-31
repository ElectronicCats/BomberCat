/*********************************************************************************
  Example WebServer with NFC Copy Cat WiFi
  by Jaz, Electronic Cats (https://electroniccats.com/)
  Date: 17/05/2022
  
  This example demonstrates how to use NFC Copy Cat by Electronic Cats
  https://github.com/ElectronicCats/NFC-Copy-Cat-WiFi

  Development environment specifics:
  IDE: Arduino 1.8.19
  Hardware Platform:
  NFC Copy Cat
  - ESP32-S2

  Electronic Cats invests time and resources providing this open source code,
  please support Electronic Cats and open-source hardware by purchasing
  products from Electronic Cats!

  This code is beerware; if you see me (or any other Electronic Cats
  member) at the local, and you've found our code helpful,
  please buy us a round!
  Distributed as-is; no warranty is given.
***********************************************************************************/

#include <Arduino.h>
#include <WiFiNINA.h>
#include "PluggableUSBMSD.h"
#include "FlashIAPBlockDevice.h"

// Create AsyncWebServer object on port 80
//AsyncWebServer server(80);
WiFiServer server(80);

// Search for parameter in HTTP POST request
const char* PARAM_INPUT_1 = "ssid";
const char* PARAM_INPUT_2 = "pass";
const char* PARAM_INPUT_3 = "ip";

//Variables to save values from HTML form
String ssid;
String pass;
String ip;

// File paths to save input values permanently
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";
const char* ipPath = "/ip.txt";

IPAddress localIP;
//IPAddress localIP(192, 168, 1, 200); // hardcoded

// Set your Gateway IP address
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 0, 0);

// Timer variables
unsigned long previousMillis = 0;
const long interval = 10000;  // interval to wait for Wi-Fi connection (milliseconds)

// Set LED GPIO
const int ledPin = 13;
// Stores LED state

//WiFi Test LED
#define led9 9

String ledState;

char ssidId[15]; //Create a Unique AP from MAC address

// Initialize SPIFFS

// Read File from SPIFFS
/*String readFile(fs::FS &fs, const char * path) {
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory()) {
    Serial.println("- failed to open file for reading");
    return String();
  }

  String fileContent;
  while (file.available()) {
    fileContent = file.readStringUntil('\n');
    break;
  }
  return fileContent;
}
*/
// Write file to SPIFFS
/*void writeFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("- file written");
  } else {
    Serial.println("- frite failed");
  }
}
*/
// Initialize WiFi
bool initWiFi() {
  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true);
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }
  return true;
}

/* //Test Code
  // Replaces placeholder with LED state value
  String processor(const String& var) {
  if (var == "STATE") {
    if (digitalRead(ledPin)) {
      ledState = "ON";
    }
    else {
      ledState = "OFF";
    }
    return ledState;
  }
  return String();
  }
*/

void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200);

  WiFiStorageFile fs = WiFiStorage.open("/fs/testfile");

  // Set GPIO 2 as an OUTPUT
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  pinMode(led9, OUTPUT);

  // Load values saved in SPIFFS
  ssid = readFile(SPIFFS, ssidPath);
  pass = readFile(SPIFFS, passPath);
  ip = readFile(SPIFFS, ipPath);
  Serial.println(ssid);
  Serial.println(pass);
  Serial.println(ip);

  if (initWiFi()) {
    /* // Route for root / web page
      server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
       request->send(SPIFFS, "/index.html", "text/html", false, processor);
      });
      server.serveStatic("/", SPIFFS, "/");

      // Route to set GPIO state to HIGH
      server.on("/on", HTTP_GET, [](AsyncWebServerRequest * request) {
       digitalWrite(ledPin, HIGH);
       request->send(SPIFFS, "/index.html", "text/html", false, processor);
      });

      // Route to set GPIO state to LOW
      server.on("/off", HTTP_GET, [](AsyncWebServerRequest * request) {
       digitalWrite(ledPin, LOW);
       request->send(SPIFFS, "/index.html", "text/html", false, processor);
      });
      server.begin();
    */
  }
  else {
    uint64_t chipid = ESP.getEfuseMac(); //The chip ID is essentially its MAC address(length: 6 bytes).
    uint16_t chip = (uint16_t)(chipid >> 32);
    snprintf(ssidId, 15, "NFC-Copy-%04X", chip);
    // Connect to Wi-Fi network with SSID and password
    Serial.println("Setting AP (Access Point)");
    // NULL sets an open Access Point
    WiFi.softAP(ssidId, NULL);

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    // Web Server Root URL
    server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
      request->send(SPIFFS, "/wifimanager.html", "text/html");
    });

    server.serveStatic("/", SPIFFS, "/");

    server.on("/", HTTP_POST, [](AsyncWebServerRequest * request) {
      int params = request->params();
      for (int i = 0; i < params; i++) {
        AsyncWebParameter* p = request->getParam(i);
        if (p->isPost()) {
          // HTTP POST ssid value
          if (p->name() == PARAM_INPUT_1) {
            ssid = p->value().c_str();
            Serial.print("SSID set to: ");
            Serial.println(ssid);
            // Write file to save value
            writeFile(SPIFFS, ssidPath, ssid.c_str());
          }
          // HTTP POST pass value
          if (p->name() == PARAM_INPUT_2) {
            pass = p->value().c_str();
            Serial.print("Password set to: ");
            Serial.println(pass);
            // Write file to save value
            writeFile(SPIFFS, passPath, pass.c_str());
          }
          // HTTP POST ip value
          if (p->name() == PARAM_INPUT_3) {
            ip = p->value().c_str();
            Serial.print("IP Address set to: ");
            Serial.println(ip);
            // Write file to save value
            writeFile(SPIFFS, ipPath, ip.c_str());
          }
          //Serial.printf("POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
        }
      }
      request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + ip);
      delay(3000);
      ESP.restart();
    });
    server.begin();
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(led9, HIGH);   // turn the LED on (HIGH is the voltage level)
    delay(1000);                       // wait for a second
    digitalWrite(led9, LOW);    // turn the LED off by making the voltage LOW
    delay(1000);
  }
}
