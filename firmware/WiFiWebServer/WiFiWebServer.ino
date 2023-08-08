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
#include <Preferences.h>  // https://github.com/ElectronicCats/Preferences
#include <SPI.h>
#include <WiFiNINA.h>
#include <ezButton.h>

#include "CloneNFCID.h"
#include "Debug.h"
#include "DetectTags.h"
#include "Magspoof.h"
#include "config.html.h"
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
#define CONFIG_URL 7

// Variables for the WiFi module
String defaultSSID = "BomberCat";
String defaultPassword = "password";
int port = 80;
WiFiServer server(port);
int status = WL_IDLE_STATUS;
int webRequest = HOME_URL;  // Default URL
bool rebootFlag = false;
unsigned long rebootTimer = 0;

Debug debug;
Preferences preferences;
ezButton button(NPIN);

// Function prototypes
void setupPreferences();
void factoryReset();
void factoryResetListener();
void setupWiFi();
void printWifiStatus();
String decodeURL(char *url);
void setupTracks();
void updateTracks(String url);
void loadPageContent(WiFiClient client);
void handleRequests();
void handleURLParameters(String url);
void runServer();
void showPageContent(WiFiClient client, const char *pageContent);

void setup() {
  Serial.begin(9600);  // Initialize serial communications with the PC

#ifdef DEBUG
  debug.setEnabled(true);
#else
  debug.setEnabled(false);
#endif

  debug.waitForSerialConnection();  // Only if debugging is enabled

  setupPreferences();
  // factoryReset();
  // factoryResetListener();

  unsigned long lastTime = millis();
  uint8_t exitCounter = 0;

  while (true) {
    button.loop();

    if (button.isPressed()) {
      exitCounter++;
      debug.println("Button pressed " + String(exitCounter) + " times");

      if (exitCounter == 3) {
        factoryReset();
        break;
      }
    }

    if (millis() - lastTime > 3000) {
      break;
    }
  }

  setupWiFi();
  setupMagspoof();
  setupTracks();
  setupNFC();
}

void loop() {
  static unsigned long lastTime = millis();
  debug.setEnabled(preferences.getBool("debug", false));

  runServer();       // Listen for incoming clients and serve the page content when connected
  handleRequests();  // Handle the requests from the client
}

void setupPreferences() {
  // Note: Namespace name is limited to 15 chars.
  const char *namespaceName = "bombercat";
  bool readOnly = false;
  preferences.begin(namespaceName, readOnly);

  // Note: Key name is limited to 15 chars.
  unsigned int rebootCounter = preferences.getUInt("rebootCounter", 0);

  rebootCounter++;
  debug.println("The BomberCat has been rebooted " + String(rebootCounter) + " times");

  // Store the rebootCounter to the Preferences
  preferences.putUInt("rebootCounter", rebootCounter);
}

void factoryReset() {
  debug.println("\nFactory reset...");
  preferences.putString("ssid", defaultSSID);
  preferences.putString("password", defaultPassword);
}

// If the button is pressed 3 times within 3 seconds, reset the preferences
void factoryResetListener() {
  unsigned long lastTime = millis();
  uint8_t exitCounter = 0;

  while (true) {
    button.loop();

    if (button.isPressed()) {
      exitCounter++;
      debug.println("Button pressed " + String(exitCounter) + " times");

      if (exitCounter == 3) {
        factoryReset();
        break;
      }
    }

    if (millis() - lastTime > 3000) {
      break;
    }
  }
}

void setupWiFi() {
  // Check for the WiFi module
  if (WiFi.status() == WL_NO_MODULE) {
    debug.println("Communication with WiFi module failed!");
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
    debug.println("Please upgrade the firmware");
  }

  String ssid = preferences.getString("ssid", defaultSSID);
  String password = preferences.getString("password", defaultPassword);
  debug.print("Creating access point named: ");
  debug.println(ssid);

  status = WiFi.beginAP(ssid.c_str(), password.c_str());
  if (status != WL_AP_LISTENING) {
    debug.println("Creating access point failed");
    // don't continue
    while (true)
      ;
  }

  server.begin();
  printWifiStatus();
}

void printWifiStatus() {
  debug.println("SSID: " + String(WiFi.SSID()));
  debug.println("Password: ", preferences.getString("password", defaultPassword));
  debug.print("To access the web interface, go to: http://");
  debug.println(WiFi.localIP());
  debug.println("Signal strength (RSSI): " + String(WiFi.RSSI()) + " dBm");
}

/// @brief Decode an URL-encoded string
/// @param url
/// @author https://arduino.stackexchange.com/questions/18007/simple-url-decoding?newreg=32c11952781c413592b3e837a3785e84
/// @return String
String decodeURL(char *url) {
  // Check if the URL starts with "%B" and return as it is.
  if (strncmp(url, "%B", 2) == 0) {
    String decodedURL = String(url);
    decodedURL.trim();
    decodedURL.replace("+", " ");
    return decodedURL;
  }

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
  String decodedURL = String(url);
  decodedURL.trim();
  decodedURL.replace("+", " ");
  return decodedURL;
}

void setupTracks() {
  String track1 = "%B123456781234567^LASTNAME/FIRST^YYMMSSSDDDDDDDDDDDDDDDDDDDDDDDDD?";
  String track2 = ";123456781234567=112220100000000000000?";

  track1 = decodeURL((char *)track1.c_str());
  track2 = decodeURL((char *)track2.c_str());

  // Copy the tracks into the char arrays using strcpy
  strcpy(tracks[0], track1.c_str());
  strcpy(tracks[1], track2.c_str());

  debug.println("Default tracks:");
  debug.print("Track 1: ");
  debug.println(tracks[0]);
  debug.print("Track 2: ");
  debug.println(tracks[1]);
}

void updateTracks(String url) {
  // Store tracks from url into String variables
  String track1 = url.substring(url.indexOf("track1=") + 7, url.indexOf("&track2="));
  String track2 = url.substring(url.indexOf("track2=") + 7, url.indexOf("&button="));

  // Decode urls
  track1 = decodeURL((char *)track1.c_str());
  track2 = decodeURL((char *)track2.c_str());

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
    case CONFIG_URL:
      showPageContent(client, config_html);
      currentHTML = CONFIG_URL;
      break;
    default:
      showPageContent(client, login_html);
      currentHTML = LOGIN_URL;
      break;
  }
}

void updateWebRequest(String url) {
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
  } else if (url.startsWith("/config.html")) {
    webRequest = CONFIG_URL;
  }
}

void handleRequests() {
  static unsigned long detectTagsTime = millis();

  if (webRequest == MAGSPOOF_URL) {
    magspoof();
  }

  // Run the NFC detect tags function every DETECT_TAGS_DELAY_MS milliseconds READ_ATTEMPTS times
  if (millis() - detectTagsTime > DETECT_TAGS_DELAY_MS && webRequest == NFC_URL && runDetectTags) {
    detectTagsTime = millis();
    nfcExecutionCounter++;

    // Wait one attempt before starting the NFC discovery
    if (nfcExecutionCounter > 1) {
      detectTags();
    }

    if (nfcExecutionCounter == READ_ATTEMPTS) {
      nfc.StopDiscovery();
      nfcExecutionCounter = 0;
      runDetectTags = false;
    }
  }

  // Reset NFC variables when the page loaded is not related with NFC
  if (webRequest != NFC_URL || clearNFCValuesFlag) {
    clearNFCValuesFlag = false;
    clearTagValues();
  }

  // Run emulateNFCID function after EMULATE_NFCID_DELAY_MS milliseconds
  if (millis() - emulateNFCIDTimer > EMULATE_NFCID_DELAY_MS && emulateNFCFlag && webRequest == NFC_URL) {
    emulateNFCID();
  }

  // Reset BomberCat after 1500 milliseconds
  if (millis() - rebootTimer > 1500 && rebootFlag) {
    // debug.println("Rebooting...");
    // rebootFlag = false;
    // NVIC_SystemReset();  // Reboot the RP2040
  }
}

/// @brief Handles the URL parameters
/// @param url The URL to handle
/// @details This function handles the URL parameters and updates the tracks and button values
void handleURLParameters(String url) {
  // ? is the start of request parameters
  if (url.startsWith("/config.html?")) {
    String btnSaveWiFiConfig = "";
    String ssid = "";
    String password = "";
    String debugStatus = "";
    int index = 0;

    index = url.indexOf("btnSaveWiFiConfig=");
    if (index != -1) {
      btnSaveWiFiConfig = url.substring(index + 18, url.indexOf("&ssid="));  // true or false
    }

    index = url.indexOf("ssid=");
    if (index != -1) {
      ssid = url.substring(index + 5, url.indexOf("&password="));
      ssid = decodeURL((char *)ssid.c_str());
    }

    index = url.indexOf("password=");
    if (index != -1) {
      password = url.substring(index + 9, url.length());
      password = decodeURL((char *)password.c_str());
    }

    index = url.indexOf("debug=");
    if (index != -1) {
      debugStatus = url.substring(index + 6, url.length());
    }

    debug.println("btnSaveWiFiConfig: ", btnSaveWiFiConfig);
    debug.println("ssid: '", ssid, "'");
    debug.println("password: '", password, "'");
    debug.println("debugStatus: '", debugStatus, "'");

    if (btnSaveWiFiConfig.startsWith("true")) {
      debug.println("Saving WiFi config...");
      preferences.putString("ssid", ssid);
      preferences.putString("password", password);
      rebootFlag = true;
      rebootTimer = millis();
    }

    if (debugStatus.startsWith("true")) {
      Serial.println("Enabling debug...");
      preferences.putBool("debug", true);
    } else if (debugStatus.startsWith("false")) {
      debug.println("Disabling debug...");
      preferences.putBool("debug", false);
    }
  } else if (url.startsWith("/nfc.html?")) {
    String btnRunDetectTags = url.substring(url.indexOf("runDetectTags=") + 14, url.length());
    String btnClear = url.substring(url.indexOf("clear=") + 6, url.length());
    String btnEmulateNFC = "";
    int index = url.indexOf("emulateState=");
    if (index != -1) {
      btnEmulateNFC = url.substring(index + 13);
    }

    if (btnClear.startsWith("true")) {
      clearNFCValuesFlag = true;
    }

    if (btnRunDetectTags.startsWith("true")) {
      mode = 1;
      resetMode();
      runDetectTags = true;
      nfcExecutionCounter = 0;
      nfcDiscoverySuccess = false;
      emulateNFCFlag = false;
    }

    if (btnEmulateNFC.startsWith("true")) {
      mode = 2;
      resetMode();
      emulateNFCFlag = true;
      debug.println("\nWaiting for reader command...");
      emulateNFCIDTimer = millis();
    } else if (btnEmulateNFC.startsWith("false")) {
      emulateNFCFlag = false;
      attempts = 0;
      nfc.StopDiscovery();
    }
  } else if (url.startsWith("/magspoof.html?")) {
    updateTracks(url);

    String button = url.substring(url.indexOf("button=") + 7, url.length());

    if (button.startsWith("Emulate")) {
      runMagspoof = true;
    }
  }
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
    client.println("let ssid = `" + preferences.getString("ssid", defaultSSID) + "`;");
    client.println("let password = `" + preferences.getString("password", defaultPassword) + "`;");
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

void runServer() {
  WiFiClient client = server.available();  // listen for incoming clients

  if (client) {  // if you get a client,
    unsigned long speedTestTime = millis();
    String currentLine = "";  // make a String to hold incoming data from the client

    while (client.connected()) {  // loop while the client's connected
      if (client.available()) {   // if there's bytes to read from the client,
        char c = client.read();   // read a byte, then
        // debug.write(c);  // print it out the serial monitor

        if (c == '\n') {  // if the byte is a newline character
          if (currentLine.length() == 0) {
            loadPageContent(client);
            break;
          } else {
            currentLine = "";  // if you got a newline, then clear currentLine:
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }

        // Only check for URL if it's a GET <url options> HTTP
        if (currentLine.startsWith("GET /") && currentLine.endsWith("HTTP/1.1")) {
          String url = currentLine.substring(4, currentLine.indexOf("HTTP/1.1"));
          // debug.println("\nRequest: " + currentLine);
          // debug.println("URL: " + url);

          updateWebRequest(url);
          handleURLParameters(url);
        }
      }
    }
    client.stop();
    // debug.println("client disconnected");
    // debug.println("Time to run server: " + String(millis() - speedTestTime) + " ms");
  }
}
