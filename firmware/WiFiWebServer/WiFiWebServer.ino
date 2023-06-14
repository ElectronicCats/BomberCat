/*********************************************************************************
  Web server for BomberCat
  by Francisco Torres, Electronic Cats (https://electroniccats.com/)
  Date: 14/04/2023

  This example demonstrates how to use a Web server for the BomberCat by Electronic Cats
  https://github.com/ElectronicCats/BomberCat

  Development environment specifics:
  IDE: Visual Studio Code + Arduino CLI Version 0.32.2
  Hardware Platform:
  BomberCat
  - RP2040

  Electronic Cats invests time and resources providing this open source code,
  please support Electronic Cats and open-source hardware by purchasing
  products from Electronic Cats!

  This code is beerware; if you see me (or any other Electronic Cats
  member) at the local, and you've found our code helpful,
  please buy us a round!
  Distributed as-is; no warranty is given.
***********************************************************************************/
#include <SPI.h>
#include <WiFiNINA.h>
// #include <Preferences.h>
// #include "Electroniccats_PN7150.h"
#include "DetectTags.h"
#include "Magspoof.h"
#include "arduino_secrets.h"
#include "home.html.h"
#include "info.html.h"
#include "login.html.h"
#include "magspoof.html.h"
#include "main.js.h"
#include "nfc.html.h"
#include "styles.css.h"

// #define DEBUG

#define CSS_URL 0
#define JAVASCRIPT_URL 1
#define LOGIN_URL 2
#define HOME_URL 3
#define INFO_URL 4
#define MAGSPOOF_URL 5
#define NFC_URL 6

char ssid[] = "BomberCat";  // your network SSID (name)
char pass[] = "password";   // your network password (use for WPA, or use as key for WEP)

WiFiServer server(80);
int status = WL_IDLE_STATUS;
int webRequest = LOGIN_URL;

// Function prototypes
String decodeURL(char *url);
void setupTracks();
void updateTracks(String url);
void loadPageContent(WiFiClient client);
void runServer();
void printWifiStatus();
void showPageContent(WiFiClient client, const char *pageContent);

void setup() {
  // Initialize serial and wait for port to open:
  Serial.begin(9600);

#ifdef DEBUG
  while (!Serial) {
    ;  // wait for serial port to connect. Needed for native USB port only
  }
#endif

  // Convertir de nuevo a una cadena de caracteres (char array)
  // strcpy(main_js, main_js_modificado.c_str());

  // Check for the WiFi module
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true)
      ;
  }

  // Set a static IP address
  IPAddress ip(10, 42, 0, 103);
  // TODO: Set a static IP address with the user preferences
  // WiFi.config(ip);

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }

  Serial.print("Creating access point named: ");
  Serial.println(ssid);

  // TODO: Set ssid and pass with user preferences
  status = WiFi.beginAP(ssid, pass);
  if (status != WL_AP_LISTENING) {
    Serial.println("Creating access point failed");
    // don't continue
    while (true)
      ;
  }

  server.begin();
  printWifiStatus();
  setupMagspoof();
  setupTracks();
  setupNFC();
}

void loop() {
  static unsigned long detectTagsTime = millis();

  runServer();
  magspoof();

  // Run the NFC detect tags function every DETECT_TAGS_DELAY_MS milliseconds READ_ATTEMPTS times
  if (millis() - detectTagsTime > DETECT_TAGS_DELAY_MS && webRequest == NFC_URL && runDetectTags) {
    detectTagsTime = millis();
    nfcExecutionCounter++;

    // Wait one attempt before starting the NFC discovery
    if (nfcExecutionCounter > 2) {
      detectTags();
    }

    if (nfcExecutionCounter == READ_ATTEMPTS) {
      nfc.StopDiscovery();
      nfcExecutionCounter = 0;
      runDetectTags = false;
    }
  }

  // Reset NFC variables when the page loaded is not related with NFC
  if (webRequest != NFC_URL || clearNFCValues) {
    clearNFCValues = false;
    cleartTagsValues();
  }
}

/// @brief Decode an URL-encoded string
/// @param url
/// @author https://arduino.stackexchange.com/questions/18007/simple-url-decoding?newreg=32c11952781c413592b3e837a3785e84
/// @return String
String decodeURL(char *url) {
  // Create two pointers that point to the start of the data
  char *leader = url;
  char *follower = leader;

  // While we're not at the end of the string (current character not NULL)
  while (*leader) {
    // Check to see if the current character is a %
    if (*leader == '%') {
      // Grab the next two characters and move leader forwards
      leader++;
      char high = *leader;
      leader++;
      char low = *leader;

      // Convert ASCII 0-9A-F to a value 0-15
      if (high > 0x39)
        high -= 7;
      high &= 0x0f;

      // Same again for the low byte:
      if (low > 0x39)
        low -= 7;
      low &= 0x0f;

      // Combine the two into a single byte and store in follower:
      *follower = (high << 4) | low;
    } else {
      // All other characters copy verbatim
      *follower = *leader;
    }

    // Move both pointers to the next character:
    leader++;
    follower++;
  }
  // Terminate the new string with a NULL character to trim it off
  *follower = 0;

  // Return the new string
  return String(url);
}

void setupTracks() {
  String track1 = "%B123456781234567^LASTNAME/FIRST^YYMMSSSDDDDDDDDDDDDDDDDDDDDDDDDD?";
  String track2 = ";123456781234567=112220100000000000000?";

  // Copy the tracks into the char arrays using strcpy
  strcpy(tracks[0], track1.c_str());
  strcpy(tracks[1], track2.c_str());

  Serial.println("Default tracks:");
  Serial.print("Track 1: ");
  Serial.println(tracks[0]);
  Serial.print("Track 2: ");
  Serial.println(tracks[1]);
}

void updateTracks(String url) {
  // Store tracks from url into String variables
  String track1 = url.substring(url.indexOf("track1=") + 7, url.indexOf("&track2="));
  String track2 = url.substring(url.indexOf("track2=") + 7, url.indexOf("&button="));

  // Decode urls
  track1 = decodeURL((char *)track1.c_str());
  track2 = decodeURL((char *)track2.c_str());

  // Remove any trailing characters
  track1.trim();
  track2.trim();

  // Replace + with spaces
  track1.replace("+", " ");
  track2.replace("+", " ");

  // Copy the tracks into the char arrays using strcpy
  strcpy(tracks[0], track1.c_str());
  strcpy(tracks[1], track2.c_str());
}

void loadPageContent(WiFiClient client) {
  static int currentHTML;

  switch (webRequest) {
    case CSS_URL:
      showPageContent(client, styles_css);
      webRequest = currentHTML;
      break;
    case JAVASCRIPT_URL:
      showPageContent(client, main_js);
      webRequest = currentHTML;
      break;
    case LOGIN_URL:
      showPageContent(client, login_html);
      currentHTML = LOGIN_URL;
      break;
    case HOME_URL:
      showPageContent(client, home_html);
      currentHTML = HOME_URL;
      break;
    case INFO_URL:
      showPageContent(client, info_html);
      currentHTML = INFO_URL;
      break;
    case MAGSPOOF_URL:
      showPageContent(client, magspoof_html);
      currentHTML = MAGSPOOF_URL;
      break;
    case NFC_URL:
      showPageContent(client, nfc_html);
      currentHTML = NFC_URL;
      break;
    default:
      showPageContent(client, login_html);
      currentHTML = LOGIN_URL;
      break;
  }
}

void runServer() {
  WiFiClient client = server.available();  // listen for incoming clients

  if (client) {                   // if you get a client,
    String currentLine = "";      // make a String to hold incoming data from the client
    while (client.connected()) {  // loop while the client's connected
      if (client.available()) {   // if there's bytes to read from the client,
        char c = client.read();   // read a byte, then
        // Serial.write(c);                    // print it out the serial monitor
        if (c == '\n') {  // if the byte is a newline character
          if (currentLine.length() == 0) {
            loadPageContent(client);
            break;
          } else {
            // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }

        // Only check for URL if it's a GET <url options> HTTP
        if (currentLine.startsWith("GET /") && currentLine.endsWith("HTTP/1.1")) {
          // Serial.println("\nRequest: " + currentLine);
          String url = currentLine.substring(4, currentLine.indexOf("HTTP/1.1"));
          // Serial.println("URL: " + url);

          if (url.startsWith("/styles.css")) {
            webRequest = CSS_URL;
          } else if (url.startsWith("/main.js")) {
            webRequest = JAVASCRIPT_URL;
          } else if (url.startsWith("/home.html?") || url.startsWith("/home.html")) {
            webRequest = HOME_URL;
          } else if (url.startsWith("/login.html")) {
            webRequest = LOGIN_URL;
          } else if (url.startsWith("/info.html")) {
            webRequest = INFO_URL;
          } else if (url.startsWith("/magspoof.html")) {
            webRequest = MAGSPOOF_URL;
          } else if (url.startsWith("/nfc.html")) {
            webRequest = NFC_URL;
          }

          // ? is the start of request parameters
          if (url.startsWith("/magspoof.html?")) {
            updateTracks(url);

            // Get the button value from the url
            String button = url.substring(url.indexOf("button=") + 7, url.length());
            // Serial.println("Button: " + button);

            if (button.startsWith("Emulate")) {
              runMagspoof = true;
            }
          }

          if (url.startsWith("/nfc.html?")) {
            String btnRunDetectTags = url.substring(url.indexOf("runDetectTags=") + 14, url.length());
            String btnClear = url.substring(url.indexOf("clear=") + 6, url.length());

            if (btnClear.startsWith("true")) {
              clearNFCValues = true;
            }

            if (btnRunDetectTags.startsWith("true")) {
              runDetectTags = true;
              nfcExecutionCounter = 0;
              nfcDiscoverySuccess = false;
            }
          }
        }
      }
    }
    client.stop();
    // Serial.println("client disconnected");
  }
}

void printWifiStatus() {
#ifdef DEBUG
  Serial.println("SSID: " + String(WiFi.SSID()));
  Serial.print("Password: ");
  Serial.println(pass);
  Serial.print("IP Address: http://");
  Serial.println(WiFi.localIP());
  Serial.println("Signal strength (RSSI): " + String(WiFi.RSSI()) + " dBm");
#endif
}

void showPageContent(WiFiClient client, const char *pageContent) {
  String contentType;
  if (webRequest == LOGIN_URL) {
    contentType = "text/html";
  } else if (webRequest == CSS_URL) {
    contentType = "text/css";
  } else if (webRequest == JAVASCRIPT_URL) {
    contentType = "application/javascript";
  }

  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:" + contentType);
  client.println();

  if (webRequest == JAVASCRIPT_URL) {
    client.println("// BomberCat Variables");
    client.println("let pollMode = `" + pollMode + "`;");
    client.println("let sensRes = `" + sensRes + "`;");
    client.println("let selRes = `" + selRes + "`;");
    client.println("let nfcID = `" + nfcID + "`;");
    client.println("let nfcDiscoverySuccess = " + String(nfcDiscoverySuccess ? "true" : "false") + ";");
  }

  // Create a temporary string to hold the page content
  char tempString[1001];
  tempString[1000] = 0;
  // Flag to indicate if we've reached the end of the page content
  boolean lastString = false;
  // Pointer to determine where we are in the page content
  char *charPtr = (char *)pageContent;

  // Loop to read the page content in chunks of 1000 bytes
  while (1) {
    for (int i = 0; i < 1000; i++) {
      // Check if we've reached the end of the page content
      if ((byte)*charPtr == 0) {
        lastString = true;
        tempString[i] = 0;
      }
      // Copy the page content to the temporary string
      tempString[i] = *charPtr;
      charPtr++;
    }
    // Send the temporary string to the client
    client.print(tempString);
    if (lastString == true)
      break;  // Exit the loop if we've reached the end of the page content
  }

  client.println("");  // Send a blank line to indicate the end of the page content
}
