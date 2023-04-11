/*
  WiFi Web Server with Files via USB for BomberCat

  This example is written for a network using WPA encryption. For
  WEP or WPA, change the WiFi.begin() call accordingly.

*/
#include "PluggableUSBMSD.h"
#include "FlashIAPBlockDevice.h"
#include <SPI.h>
#include <WiFiNINA.h>
#include "arduino_secrets.h"
#include "index.html.h"

#include "Electroniccats_PN7150.h"
#define PN7150_IRQ   (11)
#define PN7150_VEN   (13)
#define PN7150_ADDR  (0x28)

Electroniccats_PN7150 nfc(PN7150_IRQ, PN7150_VEN, PN7150_ADDR);    // creates a global NFC device interface object, attached to pins 7 (IRQ) and 8 (VEN) and using the default I2C address 0x28
RfIntf_t RfInterface;                                              //Intarface to save data for multiple tags

uint8_t mode = 1;                                                  // modes: 1 = Reader/ Writer, 2 = Emulation

///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;        // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)
int keyIndex = 0;                 // your network key index number (needed only for WEP)

int status = WL_IDLE_STATUS;

WiFiServer server(80);

char html[sizeof(index_html) + sizeof(styles_css)];

typedef enum {
  DATA_STORAGE_STATE,
  DATA_LOGGER_IDLE_STATE,
  DATA_LOGGER_RUNNING_STATE
} demo_state_e;

static FlashIAPBlockDevice bd(XIP_BASE + 0x100000, 0x100000);

USBMSD MassStorage(&bd);

static FILE *f = nullptr;

char buf[64] { 0 };

const char *fname = "/root/home.html";

void USBMSD::begin()
{
  int err = getFileSystem().mount(&bd);
  if (err) {
    err = getFileSystem().reformat(&bd);
  }
}

mbed::FATFileSystem &USBMSD::getFileSystem()
{
  static mbed::FATFileSystem fs("root");
  return fs;
}

void readContents() {
  f = fopen(fname, "r");
  if (f != nullptr) {
    while (std::fgets(buf, sizeof buf, f) != nullptr)
      Serial.print(buf);
    fclose(f);
    Serial.println("File found");
  }
  else {
    Serial.println("File not found");
  }
}

void showWebPage(WiFiClient client);
void showCSS(WiFiClient client);

void setup() {
  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  MassStorage.begin();
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

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

  //readContents();
  // sprintf(html, index_html, styles_css);
}

void loop() {
  runServer();
}

void runServer() {
  WiFiClient client = server.available();   // listen for incoming clients

  if (client) {                             // if you get a client,
    Serial.println("new client");           // print a message out the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        if (c == '\n') {                    // if the byte is a newline character
          if (currentLine.length() == 0) {
            showWebPage(client);
            break;
          } else {
            // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }

        // Check to see if the client request was /X
        if (currentLine.endsWith("GET /MGS")) {
          //MagSpoof Code
          Serial.println("MGS");
        }
        if (currentLine.endsWith("GET /DT")) {
          //Detect Tags Code
          // setupDetectTags();
          // loopdetectTags();
          Serial.println("Here");
        }
        if (currentLine.endsWith("GET /BMC")) {
          //BMC Code
          Serial.println("BMC");
        }
      }
    }
    client.stop(); // close the connection:
    Serial.println("client disconnected");
  }
}

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}

void showWebPage(WiFiClient client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println();
  client.println(index_html);
}

/////////////////////////////////

void ResetMode() {                                 //Reset the configuration mode after each reading
  WiFiClient client = server.available();
  client.println("Re-initializing...");
  nfc.ConfigMode(mode);
  nfc.StartDiscovery(mode);
}

void PrintBuf(const byte * data, const uint32_t numBytes) { //Print hex data buffer in format
  WiFiClient client = server.available();
  uint32_t szPos;
  for (szPos = 0; szPos < numBytes; szPos++)
  {
    client.print(F("0x"));
    // Append leading 0 for small values
    if (data[szPos] <= 0xF)
      client.print(F("0"));
    client.print(data[szPos] & 0xff, HEX);
    if ((numBytes > 1) && (szPos != numBytes - 1))
    {
      client.print(F(" "));
    }
  }
  client.println();
}
void displayCardInfo(RfIntf_t RfIntf) { //Funtion in charge to show the card/s in te field
  WiFiClient client = server.available();
  char tmp[16];
  while (1) {
    switch (RfIntf.Protocol) { //Indetify card protocol
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

    switch (RfIntf.ModeTech) { //Indetify card technology
      case (MODE_POLL | TECH_PASSIVE_NFCA):
        client.print("\tSENS_RES = ");
        sprintf(tmp, "0x%.2X", RfIntf.Info.NFC_APP.SensRes[0]);
        client.print(tmp); client.print(" ");
        sprintf(tmp, "0x%.2X", RfIntf.Info.NFC_APP.SensRes[1]);
        client.print(tmp); client.println(" ");

        client.print("\tNFCID = ");
        PrintBuf(RfIntf.Info.NFC_APP.NfcId, RfIntf.Info.NFC_APP.NfcIdLen);

        if (RfIntf.Info.NFC_APP.SelResLen != 0) {
          client.print("\tSEL_RES = ");
          sprintf(tmp, "0x%.2X", RfIntf.Info.NFC_APP.SelRes[0]);
          client.print(tmp); client.println(" ");

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
    if (RfIntf.MoreTags) { // It will try to identify more NFC cards if they are the same technology
      if (nfc.ReaderActivateNext(&RfIntf) == NFC_ERROR) break;
    }
    else break;
  }
}

void setupDetectTags() {
  WiFiClient client = server.available();

  client.println("Detect NFC tags with PN7150");

  client.println("Initializing...");
  if (nfc.connectNCI()) { //Wake up the board
    client.println("Error while setting up the mode, check connections!");
    while (1);
  }

  if (nfc.ConfigureSettings()) {
    client.println("The Configure Settings is failed!");
    while (1);
  }

  if (nfc.ConfigMode(mode)) { //Set up the configuration mode
    client.println("The Configure Mode is failed!!");
    while (1);
  }
  nfc.StartDiscovery(mode); //NCI Discovery mode
  client.println("BomberCat, Yes Sir!");
  client.println("Waiting for an Card ...");
}

void loopdetectTags() {
  WiFiClient client = server.available();
  if (!nfc.WaitForDiscoveryNotification(&RfInterface)) { // Waiting to detect cards
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
