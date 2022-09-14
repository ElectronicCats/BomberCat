/************************************************************
  Example for read NFC card via MQTT version for BomberCat
  by Andres Sabas, Electronic Cats (https://electroniccats.com/)
  by Salvador Mendoza (salmg.net)
  Date: 17/05/2022

  This example demonstrates how to use BomberCat by Electronic Cats


  Development environment specifics:
  IDE: Arduino 1.8.19
  Hardware Platform:
  BomberCat
  - RP2040

  Thanks Wallee for support this project open source https://en.wallee.com/

  Electronic Cats invests time and resources providing this open source code,
  please support Electronic Cats and open-source hardware by purchasing
  products from Electronic Cats!

  This code is beerware; if you see me (or any other Electronic Cats
  member) at the local, and you've found our code helpful,
  please buy us a round!
  Distributed as-is; no warranty is given.
  /************************************************************/

#include "arduino_secrets.h"
#include <SPI.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>
#include <SerialCommand.h>
#include "Electroniccats_PN7150.h"

//#define DEBUG
#define SERIALCOMMAND_HARDWAREONLY
#define PERIOD 20000
#define HOST 0

#include <FlashIAPBlockDevice.h>
#include <TDBStore.h>

using namespace mbed;

// Get limits of the In Application Program (IAP) flash, ie. the internal MCU flash.
#include "FlashIAPLimits.h"
auto iapLimits { getFlashIAPLimits() };

// Create a block device on the available space of the FlashIAP
FlashIAPBlockDevice blockDevice(iapLimits.start_address, iapLimits.available_size);

// Create a key-value store on the Flash IAP block device
TDBStore store(&blockDevice);

SerialCommand SCmd;

float fwVersion = 0.2;

// Update these with values suitable for your network.
char mqtt_server[] = mqttServ;
char ssid[255] = SECRET_SSID;        // your network SSID (name)
char pass[255] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)

auto result = 0;
// An example key name for the stats on the store
const char statsKey[] { "stats" };

// Dummy sketch stats data for demonstration purposes
struct SketchStats {
  char ssidStore[255];
  char passwordStore[255];
  char mqttStore[255];
};

// Previous stats
SketchStats previousStats;

boolean flagWifi, flagMqtt, flagStore = false;

char outTopic[] = "RelayHost#";
char inTopic[] = "RelayClient#";

char buf[] = "Hello I'm here Host #";

boolean host_selected = false;
unsigned long tiempo = 0;

// Create a client ID
String clientId = "BomberCatHost-CARD00";

// tracks
const char* tracks[] = {
"%B123456781234567^LASTNAME/FIRST^YYMMSSSDDDDDDDDDDDDDDDDDDDDDDDDD?\0", // Track 1
";123456781234567=112220100000000000000?\0" // Track 2
};

#define L1         (LED_BUILTIN)  //LED1 indicates activity

#define NPIN       (5) //Button

WiFiClient espClient;
int status = WL_IDLE_STATUS;

PubSubClient client(espClient);
unsigned long lastMsg = 0;

#define PN7150_IRQ   (11)
#define PN7150_VEN   (13)
#define PN7150_ADDR  (0x28)

Electroniccats_PN7150 nfc(PN7150_IRQ, PN7150_VEN, PN7150_ADDR); // creates a global NFC device interface object, attached to pins 7 (IRQ) and 8 (VEN) and using the default I2C address 0x28
RfIntf_t RfInterface;

uint8_t mode = 2;                                                  // modes: 1 = Reader/ Writer, 2 = Emulation

uint8_t commandlarge = 0;

//Visa MSD emulation variables
uint8_t apdubuffer[255] = {}, apdulen;

uint8_t ppse[255];
boolean detectCardFlag = false;

/*****************
   File System
 ********************/
// Retrieve SketchStats from the key-value store
int getSketchStats(const char* key, SketchStats* stats)
{
  // Retrieve key-value info
  TDBStore::info_t info;
  auto result = store.get_info(key, &info);

  if (result == MBED_ERROR_ITEM_NOT_FOUND)
    return result;

  // Allocate space for the value
  uint8_t buffer[info.size] {};
  size_t actual_size;

  // Get the value
  result = store.get(key, buffer, sizeof(buffer), &actual_size);
  if (result != MBED_SUCCESS)
    return result;

  memcpy(stats, buffer, sizeof(SketchStats));
  return result;
}

// Store a SketchStats to the the k/v store
int setSketchStats(const char* key, SketchStats stats)
{
  return store.set(key, reinterpret_cast<uint8_t*>(&stats), sizeof(SketchStats), 0);
}
/*****************
       NFC
 *****************/
void resetMode() { //Reset the configuration mode after each reading
  #ifdef DEBUG
  Serial.println("Reset...");
  #endif
  if (nfc.connectNCI()) { //Wake up the board
    Serial.println("Error while setting up the mode, check connections!");
    while (1);
  }

  if (nfc.ConfigureSettings()) {
    Serial.println("The Configure Settings failed!");
    while (1);
  }

  if (nfc.ConfigMode(mode)) { //Set up the configuration mode
    Serial.println("The Configure Mode failed!!");
    while (1);
  }

  nfc.StartDiscovery(mode); //NCI Discovery mode
}

//Print hex data buffer in format
void printBuf(const byte * data, const uint32_t numBytes) {
  uint32_t szPos;
  for (szPos = 0; szPos < numBytes; szPos++) {
    Serial.print(F("0x"));
    // Append leading 0 for small values
    if (data[szPos] <= 0xF)
      Serial.print(F("0"));

    Serial.print(data[szPos] & 0xff, HEX);
    if ((numBytes > 1) && (szPos != numBytes - 1)) {
      Serial.print(F(" "));
    }
  }
  Serial.println();
}

void printData(uint8_t *buff, uint8_t lenbuffer, uint8_t cmd) {
  char tmp[1];

  if (cmd == 1)
    Serial.print("\nCommand: ");
  else if (cmd == 2)
    Serial.print("\nReader command: ");
  else if (cmd == 3)
    Serial.print("\nHunter Cat answer: ");
  else
    Serial.print("\nCard answer: ");

  for (uint8_t i = 0; i < lenbuffer; i++) {
    Serial.print("0x");
    Serial.print(buff[i] < 16 ? "0" : "");
    Serial.print(buff[i], HEX);
    Serial.print(" ");
  }

  Serial.println();
}

//Find Track 2 in the NFC reading transaction
void seekTrack2() {
  #ifdef DEBUG
  Serial.println("Send data to Card...");
  #endif
  uint8_t apdubuffer[255] = {}, apdulen;

  //blink(L2, 150, 1);
  #ifdef DEBUG
  printData(ppse, commandlarge, 1);
  #endif

  // Send command from terminal to card
  nfc.CardModeSend(ppse, commandlarge);

  while (nfc.CardModeReceive(apdubuffer, &apdulen) != 0) { }

  if (nfc.CardModeReceive(apdubuffer, &apdulen) == 0) {
    #ifdef DEBUG
    printData(apdubuffer, apdulen, 4);
    #endif
    
    client.publish(outTopic, apdubuffer, apdulen);
    tiempo = millis();  // da mas tiempo antes de cerrar la conexión
  }
  else {
    Serial.println("Error reading the card!");
  }
}

//Is it a card in range? for Mifare and ISO cards
void detectcard() {
  while (detectCardFlag == false) {
    #ifdef DEBUG
    Serial.println("wait detect Card...");
    #endif
    if (!nfc.WaitForDiscoveryNotification(&RfInterface)) { // Waiting to detect cards

      if (RfInterface.ModeTech == MODE_POLL || RfInterface.ModeTech == TECH_PASSIVE_NFCA) {
        char tmp[16];
        #ifdef DEBUG
        Serial.print("\tSENS_RES = ");
        sprintf(tmp, "0x%.2X", RfInterface.Info.NFC_APP.SensRes[0]);
        Serial.print(tmp); Serial.print(" ");
        sprintf(tmp, "0x%.2X", RfInterface.Info.NFC_APP.SensRes[1]);
        Serial.print(tmp); Serial.println(" ");
        Serial.print("\tNFCID = ");
        printBuf(RfInterface.Info.NFC_APP.NfcId, RfInterface.Info.NFC_APP.NfcIdLen);
        #endif

        if (RfInterface.Info.NFC_APP.NfcIdLen != 4) {

          Serial.println("Ooops ... this doesn't seem to be a Mifare Classic card!");
          return;
        }

        if (RfInterface.Info.NFC_APP.SelResLen != 0) {
          #ifdef DEBUG
          Serial.print("\tSEL_RES = ");
          sprintf(tmp, "0x%.2X", RfInterface.Info.NFC_APP.SelRes[0]);
          Serial.print(tmp); Serial.println(" ");
          #endif
        }
      }
      switch (RfInterface.Protocol) {
        case PROT_ISODEP:

          #ifdef DEBUG
          Serial.println(" - Found ISODEP card");
          #endif

          seekTrack2();
          break;

        case PROT_MIFARE:
          Serial.println(" - Found MIFARE card");
          break;

        default:
          Serial.println(" - Not a valid card");
          break;
      }

      //* It can detect multiple cards at the same time if they use the same protocol
      /*if (RfInterface.MoreTags) {
        nfc.ReaderActivateNext(&RfInterface);
      }*/

      //* Wait for card removal
      //nfc.ProcessReaderMode(RfInterface, PRESENCE_CHECK);
      //Serial.println("CARD REMOVED!");

      //nfc.StopDiscovery();
      //nfc.StartDiscovery(mode);
      detectCardFlag = true;
    }
  }
}

//To read Mifare and Visa
void mifarevisa() {
  if(detectCardFlag == false){
    detectcard();
  }
  else{
    seekTrack2();
  }
  //detectCardFlag = false;
}
/*****************
       WIFI
 *****************/
void setup_wifi() {
  if (!flagStore) {
    char *arg;
    arg = SCmd.next();    // Get the next argument from the SerialCommand object buffer
    if (arg != NULL) {
      strcpy(ssid, arg);
      Serial.print("First argument was: ");
      Serial.println(ssid);
    }
    else {
      Serial.println("No arguments for WiFi");
    }

    arg = SCmd.next();
    if (arg != NULL) {
      strcpy(pass, arg);
      Serial.print("Second argument was: ");
      Serial.println(pass);
    }
    else {
      Serial.println("No second argument for pass");
    }
  }
  else {
    result = getSketchStats(statsKey, &previousStats);

    strcpy(ssid, previousStats.ssidStore);
    strcpy(pass, previousStats.passwordStore);
  }

  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

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

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }

  if (!flagStore) {
    result = getSketchStats(statsKey, &previousStats);

    strcpy(previousStats.ssidStore, ssid);
    strcpy(previousStats.passwordStore, pass);

    result = setSketchStats(statsKey, previousStats);

    if (result == MBED_SUCCESS) {
      Serial.println("Save WiFiSetup in Stats Flash");
      Serial.print("\tSSID: ");
      Serial.println(previousStats.ssidStore);
      Serial.print("\tWiFiPass: ");
      Serial.println(previousStats.passwordStore);
      Serial.print("\tMQTT Server: ");
      Serial.println(previousStats.mqttStore);

    } else {
      Serial.println("Error while saving to key-value store");
      while (true);
    }
  }
  flagWifi = true;
}

/*****************
       MQTT
 *****************/

void setup_mqtt() {
  if (!flagStore) {
    char *arg;
    arg = SCmd.next();    // Get the next argument from the SerialCommand object buffer
    if (arg != NULL) {    // As long as it existed, take it
      strcpy(mqtt_server, arg);
      Serial.print("MQTT Server: ");
      Serial.println(mqtt_server);
    }
    else {
      Serial.println("No arguments for MQTTServer");
    }
  }
  Serial.print("Connecting MQTT to ");
  Serial.println(mqtt_server);
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  if (!flagStore) {
    result = getSketchStats(statsKey, &previousStats);

    strcpy(previousStats.mqttStore, mqtt_server);


    result = setSketchStats(statsKey, previousStats);

    if (result == MBED_SUCCESS) {
      Serial.println("Save MQTTSetup in Stats Flash");
      Serial.print("\tSSID: ");
      Serial.println(previousStats.ssidStore);
      Serial.print("\tWiFiPass: ");
      Serial.println(previousStats.passwordStore);
      Serial.print("\tMQTT Server: ");
      Serial.println(previousStats.mqttStore);
    } else {
      Serial.println("Error while saving to key-value store");
      while (true);
    }
  }
  flagMqtt = true;
  Serial.println("connected MQTT");
}

//Callback MQTT suscribe to inTopic from RelayClient
void callback(char* topic, byte * payload, unsigned int length) {
  #ifdef DEBUG
  Serial.print("Host Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  #endif

  // update host status check if there is a client requesting
  if (strcmp(topic,"hosts") == 0) {
    if(payload[HOST] != '#'){          // the host is requested
      inTopic[11] = payload[HOST];     // change client number
      host_selected = true;
      tiempo = millis();            // reset time
      client.subscribe(inTopic);
            
      Serial.println(inTopic);
      
      return;
    }
  }

  if (strcmp(topic,inTopic) == 0 && host_selected) {

    if (strcmp(payload,"MS") == 0){
      client.publish(outTopic, tracks);
    }
    
    commandlarge = length;
    for (int i = 0; i < length; i++) {
  
      ppse[i] = payload[i];
      #ifdef DEBUG
      Serial.print(payload[i], HEX);
      #endif
    }
    #ifdef DEBUG
    Serial.println();
    #endif
    mifarevisa();
  }
}
// Connect and reconnect to MQTT
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println(" connected");
      // Once connected, publish an announcement...
      //client.publish("status", "Hello I'm here Host ");
      buf[20] = HOST + 48;
      client.publish("status", buf);
      // ... and resubscribe
      client.subscribe("hosts");
      Serial.println("Subscribed to the hosts topic");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void blink(int pin, int msdelay, int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(msdelay);
    digitalWrite(pin, LOW);
    delay(msdelay);
  }
}

void setup() {
  pinMode(L1, OUTPUT);
  pinMode(NPIN, INPUT_PULLUP);

  Serial.begin(9600);
  #ifdef DEBUG
  while (!Serial);
  #endif
  
  mode = 1;
  resetMode();

  // Get limits of the the internal flash of the microcontroller
  auto [flashSize, startAddress, iapSize] = getFlashIAPLimits();

  Serial.print("Flash Size: ");
  Serial.print(flashSize / 1024.0 / 1024.0);
  Serial.println(" MB");
  Serial.print("FlashIAP Start Address: 0x");
  Serial.println(startAddress, HEX);
  Serial.print("FlashIAP Size: ");
  Serial.print(iapSize / 1024.0 / 1024.0);
  Serial.println(" MB");

  // Create a block device on the available space of the flash
  FlashIAPBlockDevice blockDevice(startAddress, iapSize);

  // Initialize the Flash IAP block device and print the memory layout
  blockDevice.init();

  // Initialize the key-value store
  Serial.print("Initializing TDBStore: ");
  auto result = store.init();
  Serial.println(result == MBED_SUCCESS ? "OK" : "Failed");
  if (result != MBED_SUCCESS)
    while (true); // Stop the sketch if an error occurs

  // Get previous run stats from the key-value store
  Serial.println("Retrieving Sketch Stats");
  result = getSketchStats(statsKey, &previousStats);

  if (result == MBED_SUCCESS) {
    Serial.println("Previous Setup Stats WiFi and MQTT");
    Serial.print("\tSSID: ");
    Serial.println(previousStats.ssidStore);
    Serial.print("\tWiFiPass: ");
    Serial.println(previousStats.passwordStore);
    Serial.print("\tMQTT Server: ");
    Serial.println(previousStats.mqttStore);
    flagStore = true;
  } else if (result == MBED_ERROR_ITEM_NOT_FOUND) {
    Serial.println("No previous data for wifi and mqtt was found.");
    Serial.println("Run setup_wifi command.");
    Serial.println("Run setup_mqtt command.");
  } else {
    Serial.println("Error reading from key-value store.");
    while (true);
  }

  if (flagStore == true) {
    setup_wifi();
  }
  if ((flagWifi == true) && (flagStore == true)) {
    setup_mqtt();
  }

  // blink to show we started up
  blink(L1, 200, 6);

  Serial.println("BomberCat, yes Sir!");
  Serial.println("Host Relay NFC");

  outTopic[9] = HOST + 48;
  
  Serial.println(outTopic);
  if (flagMqtt == 1) {
    reconnect();
  }

  Serial.println("BomberCat, yes Sir!");
  Serial.println("Host Relay NFC");
  Serial.println("Welcome to the BomberCat CLI " + String(fwVersion, 1) + "v\n");
  Serial.println("Type help to get the available commands.");
  Serial.println("Electronic Cats ® 2022");

  // Setup callbacks for SerialCommand commands
  SCmd.addCommand("help", help);
  SCmd.addCommand("setup_wifi", setup_wifi);
  SCmd.addCommand("setup_mqtt", setup_mqtt);

  SCmd.setDefaultHandler(unrecognized);  // Handler for command that isn't matched  (says "What?")
}

void loop() { // Main loop

  // procesa comandos seriales
  SCmd.readSerial();
/*
  if (flagMqtt == true) {
    if (!client.connected()) {
      reconnect();
    }
  }
*/
  if (flagMqtt == 1) {
    // procesa mensajes MQTT
    client.loop();
  }

  if((millis() - tiempo) > PERIOD && host_selected){
    // RESET host connection
    host_selected = false;
    client.unsubscribe(inTopic);
    Serial.println("The host connection is terminated.");
    apdubuffer[0] = NULL;
    apdulen = 0;
    reconnect();
  }
}

void help() {
  Serial.println("Fw version: " + String(fwVersion, 1) + "v");
  Serial.println("\tConfiguration commands:");
  Serial.println("\tsetup_wifi");
  Serial.println("\tsetup_mqtt");

  Serial.println("Monitor commands:");
  Serial.println("\tget_hs");
  Serial.println("..help");
}
// This gets set as the default handler, and gets called when no other command matches.
void unrecognized(const char *command) {
  Serial.println("Command not found, type help to get the valid commands");
}
