/*********************************************************************************
  Example WebServer with NFC Copy Cat WiFi
  by Francisco Torres, Electronic Cats (https://electroniccats.com/)
  Date: 14/04/2023

  This example demonstrates how to use NFC Copy Cat by Electronic Cats
  https://github.com/ElectronicCats/NFC-Copy-Cat-WiFi

  Development environment specifics:
  IDE: Visual Studio Code + Arduino CLI
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
#include <SPI.h>
#include <WiFiNINA.h>
// #include <Preferences.h>
#include "Electroniccats_PN7150.h"
#include "arduino_secrets.h"
#include "home.html.h"
#include "info.html.h"
#include "login.html.h"
#include "magspoof.html.h"
#include "main.js.h"
#include "styles.css.h"

#define PN7150_IRQ (11)
#define PN7150_VEN (13)
#define PN7150_ADDR (0x28)

#define CSS_URL 0
#define JAVASCRIPT_URL 1
#define LOGIN_URL 2
#define HOME_URL 3
#define INFO_URL 4
#define MAGSPOOF_URL 5

// Magspoof consts
#define L1 (LED_BUILTIN)  // LED1
#define PIN_A (6)         // MagSpoof-1
#define PIN_B (7)         // MagSpoof
#define NPIN (5)          // Button
#define CLOCK_US (500)
#define BETWEEN_ZERO (53)  // 53 zeros between track1 & 2
#define TRACKS (2)
#define DEBUGCAT

Electroniccats_PN7150 nfc(PN7150_IRQ, PN7150_VEN, PN7150_ADDR);  // creates a global NFC device interface object, attached to pins 7 (IRQ) and 8 (VEN) and using the default I2C address 0x28
RfIntf_t RfInterface;                                            // Intarface to save data for multiple tags

uint8_t mode = 1;  // modes: 1 = Reader/ Writer, 2 = Emulation

///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;  // your network SSID (name)
char pass[] = SECRET_PASS;  // your network password (use for WPA, or use as key for WEP)
int keyIndex = 0;           // your network key index number (needed only for WEP)

WiFiServer server(80);
int status = WL_IDLE_STATUS;
int webRequest = LOGIN_URL;

void runServer();
void printWifiStatus();
void showPageContent(WiFiClient client, const char* pageContent);

void setup() {
  // Initialize serial and wait for port to open:
  Serial.begin(9600);

  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true)
      ;
  }

  // Set a static IP address
  IPAddress ip(10, 42, 0, 103);
  // WiFi.config(ip);

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }

  // attempt to connect to WiFi network:
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    status = WiFi.begin(ssid, pass);

    // wait 10 seconds for connection:
    delay(10000);
  }
  server.begin();
  // you're connected now, so print out the status:
  printWifiStatus();

  magspoofSetup();
}

void loop() {
  runServer();
  magspoof();
}

void runServer() {
  WiFiClient client = server.available();  // listen for incoming clients
  static int currentHTML;

  if (client) {                   // if you get a client,
    String currentLine = "";      // make a String to hold incoming data from the client
    while (client.connected()) {  // loop while the client's connected
      if (client.available()) {   // if there's bytes to read from the client,
        char c = client.read();   // read a byte, then
        // Serial.write(c);                    // print it out the serial monitor
        if (c == '\n') {  // if the byte is a newline character
          if (currentLine.length() == 0) {
            if (webRequest == LOGIN_URL) {
              showPageContent(client, login_html);
              currentHTML = LOGIN_URL;
            } else if (webRequest == CSS_URL) {
              showPageContent(client, styles_css);
              webRequest = currentHTML;
            } else if (webRequest == JAVASCRIPT_URL) {
              showPageContent(client, main_js);
              webRequest = currentHTML;
            } else if (webRequest == HOME_URL) {
              showPageContent(client, home_html);
              currentHTML = HOME_URL;
            } else if (webRequest == INFO_URL) {
              showPageContent(client, info_html);
              currentHTML = INFO_URL;
            } else if (webRequest == MAGSPOOF_URL) {
              showPageContent(client, magspoof_html);
              currentHTML = MAGSPOOF_URL;
            }

            break;
          } else {
            // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }

        // Only check for URL if it's a GET <url options> HTTP/ (ignore the http version number)
        if (currentLine.startsWith("GET /") && currentLine.endsWith("HTTP/1.1")) {
          Serial.println("\nRequest: " + currentLine);
          String url = currentLine.substring(4, currentLine.indexOf("HTTP/1.1"));
          Serial.println("URL: " + url);

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
          }

          // ? is the start of request parameters
          if (url.startsWith("/magspoof.html?")) {
            String track1 = url.substring(url.indexOf("track1=") + 7, url.indexOf("&track2="));
            String track2 = url.substring(url.indexOf("track2=") + 7, url.indexOf("&button="));
            String button = url.substring(url.indexOf("button=") + 7, url.length());
            Serial.println("Track 1: " + track1);
            Serial.println("Track 2: " + track2);
            Serial.println("Button: " + button);
          }
        }
      }
    }
    client.stop();  // close the connection:
    Serial.println("client disconnected");
  }
}

void printWifiStatus() {
  Serial.println("SSID: " + String(WiFi.SSID()));
  Serial.print("IP Address: http://");
  Serial.println(WiFi.localIP());
  Serial.println("Signal strength (RSSI): " + String(WiFi.RSSI()) + " dBm");
}

void showPageContent(WiFiClient client, const char* pageContent) {
  String contentType;
  if (webRequest == LOGIN_URL) {
    contentType = "text/html";
  } else if (webRequest == CSS_URL) {
    contentType = "text/css";
  }
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:" + contentType);
  client.println();

  // Create a temporary string to hold the page content
  char tempString[1001];
  tempString[1000] = 0;
  // Flag to indicate if we've reached the end of the page content
  boolean lastString = false;
  // Pointer to determine where we are in the page content
  char* charPtr = (char*)pageContent;

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
    if (lastString == true) break;  // Exit the loop if we've reached the end of the page content
  }
  client.println("");  // Send a blank line to indicate the end of the page content
}

void ResetMode() {  // Reset the configuration mode after each reading
  WiFiClient client = server.available();
  client.println("Re-initializing...");
  nfc.ConfigMode(mode);
  nfc.StartDiscovery(mode);
}

void PrintBuf(const byte* data, const uint32_t numBytes) {  // Print hex data buffer in format
  WiFiClient client = server.available();
  uint32_t szPos;
  for (szPos = 0; szPos < numBytes; szPos++) {
    client.print(F("0x"));
    // Append leading 0 for small values
    if (data[szPos] <= 0xF)
      client.print(F("0"));
    client.print(data[szPos] & 0xff, HEX);
    if ((numBytes > 1) && (szPos != numBytes - 1)) {
      client.print(F(" "));
    }
  }
  client.println();
}

void displayCardInfo(RfIntf_t RfIntf) {  // Funtion in charge to show the card/s in te field
  WiFiClient client = server.available();
  char tmp[16];
  while (1) {
    switch (RfIntf.Protocol) {  // Indetify card protocol
      case PROT_T1T:
      case PROT_T2T:
      case PROT_T3T:
      case PROT_ISODEP:
        client.print(" - POLL MODE: Remote activated tag type: ");
        client.println(RfIntf.Protocol);
        break;
      case PROT_ISO15693:
        client.println(" - POLL MODE: Remote ISO15693 card activated");
        break;
      case PROT_MIFARE:
        client.println(" - POLL MODE: Remote MIFARE card activated");
        break;
      default:
        client.println(" - POLL MODE: Undetermined target");
        return;
    }

    switch (RfIntf.ModeTech) {  // Indetify card technology
      case (MODE_POLL | TECH_PASSIVE_NFCA):
        client.print("\tSENS_RES = ");
        sprintf(tmp, "0x%.2X", RfIntf.Info.NFC_APP.SensRes[0]);
        client.print(tmp);
        client.print(" ");
        sprintf(tmp, "0x%.2X", RfIntf.Info.NFC_APP.SensRes[1]);
        client.print(tmp);
        client.println(" ");

        client.print("\tNFCID = ");
        PrintBuf(RfIntf.Info.NFC_APP.NfcId, RfIntf.Info.NFC_APP.NfcIdLen);

        if (RfIntf.Info.NFC_APP.SelResLen != 0) {
          client.print("\tSEL_RES = ");
          sprintf(tmp, "0x%.2X", RfIntf.Info.NFC_APP.SelRes[0]);
          client.print(tmp);
          client.println(" ");
        }
        break;

      case (MODE_POLL | TECH_PASSIVE_NFCB):
        if (RfIntf.Info.NFC_BPP.SensResLen != 0) {
          client.print("\tSENS_RES = ");
          PrintBuf(RfIntf.Info.NFC_BPP.SensRes, RfIntf.Info.NFC_BPP.SensResLen);
        }
        break;

      case (MODE_POLL | TECH_PASSIVE_NFCF):
        client.print("\tBitrate = ");
        client.println((RfIntf.Info.NFC_FPP.BitRate == 1) ? "212" : "424");

        if (RfIntf.Info.NFC_FPP.SensResLen != 0) {
          client.print("\tSENS_RES = ");
          PrintBuf(RfIntf.Info.NFC_FPP.SensRes, RfIntf.Info.NFC_FPP.SensResLen);
        }
        break;

      case (MODE_POLL | TECH_PASSIVE_15693):
        client.print("\tID = ");
        PrintBuf(RfIntf.Info.NFC_VPP.ID, sizeof(RfIntf.Info.NFC_VPP.ID));

        client.print("\ntAFI = ");
        client.println(RfIntf.Info.NFC_VPP.AFI);

        client.print("\tDSFID = ");
        client.println(RfIntf.Info.NFC_VPP.DSFID, HEX);
        break;

      default:
        break;
    }
    if (RfIntf.MoreTags) {  // It will try to identify more NFC cards if they are the same technology
      if (nfc.ReaderActivateNext(&RfIntf) == NFC_ERROR) break;
    } else
      break;
  }
}

void setupDetectTags() {
  WiFiClient client = server.available();

  client.println("Detect NFC tags with PN7150");

  client.println("Initializing...");
  if (nfc.connectNCI()) {  // Wake up the board
    client.println("Error while setting up the mode, check connections!");
    while (1)
      ;
  }

  if (nfc.ConfigureSettings()) {
    client.println("The Configure Settings is failed!");
    while (1)
      ;
  }

  if (nfc.ConfigMode(mode)) {  // Set up the configuration mode
    client.println("The Configure Mode is failed!!");
    while (1)
      ;
  }
  nfc.StartDiscovery(mode);  // NCI Discovery mode
  client.println("BomberCat, Yes Sir!");
  client.println("Waiting for an Card ...");
}

void loopdetectTags() {
  WiFiClient client = server.available();
  if (!nfc.WaitForDiscoveryNotification(&RfInterface)) {  // Waiting to detect cards
    displayCardInfo(RfInterface);
    switch (RfInterface.Protocol) {
      case PROT_T1T:
      case PROT_T2T:
      case PROT_T3T:
      case PROT_ISODEP:
        nfc.ProcessReaderMode(RfInterface, READ_NDEF);
        break;

      case PROT_ISO15693:
        break;

      case PROT_MIFARE:
        nfc.ProcessReaderMode(RfInterface, READ_NDEF);
        break;

      default:
        break;
    }

    //* It can detect multiple cards at the same time if they use the same protocol
    if (RfInterface.MoreTags) {
      nfc.ReaderActivateNext(&RfInterface);
    }
    //* Wait for card removal
    nfc.ProcessReaderMode(RfInterface, PRESENCE_CHECK);
    client.println("CARD REMOVED!");

    nfc.StopDiscovery();
    nfc.StartDiscovery(mode);
  }
  ResetMode();
  delay(500);
}
