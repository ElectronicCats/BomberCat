/************************************************************
  Example for read NFC card via MQTT version for BomberCat
  by Andres Sabas, Electronic Cats (https://electroniccats.com/)
  by Raul Vargas
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
#define PERIOD 10000
#define HOST 0
#define HMAX 42

// Create a client ID
char hostId[] = "BomberCatHost-CARD##";

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

int debug = 0;
int nhost;

// tracks
char tracks[255];

auto result = 0;
// An example key name for the stats on the store
const char statsKey[] { "stats" };

// Dummy sketch stats data for demonstration purposes
struct SketchStats {
  char ssidStore[255];
  char passwordStore[255];
  char mqttStore[255];
  char trackStore[255];
  int nhostStore;
};

// Previous stats
SketchStats previousStats;

int flagWifi, flagMqtt, flagStore = 0;

char outTopic[] = "RelayHost##";
char inTopic[] = "RelayClient##";

char dhost[] = "h##c##";

char buf[] = "Hello I'm here Host ##";

int host_selected = 0;
unsigned long tiempo = 0;

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
int detectCardFlag = 0;

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
if(debug) {
  Serial.println("Reset...");
}
  if (nfc.connectNCI()) { //Wake up the board
    if(debug) {
      Serial.println("Error while setting up the mode, check connections!");
    }  
    Serial.println("ERROR");
    while (1);
  }

  if (nfc.ConfigureSettings()) {
    if(debug) {
      Serial.println("The Configure Settings failed!");
    }
    Serial.println("ERROR");  
    while (1);
  }

  if (nfc.ConfigMode(mode)) { //Set up the configuration mode
    if(debug) {
      Serial.println("The Configure Mode failed!!");
    }
    Serial.println("ERROR");  
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
  uint8_t apdubuffer[255] = {}, apdulen;
if(debug) {
  Serial.println("Send data to Card...");
  printData(ppse, commandlarge, 1);
}

  // Send command from terminal to card
  nfc.CardModeSend(ppse, commandlarge);

  
  Serial.println("sale de nfc.CardModeSend...");
  
  while (nfc.CardModeReceive(apdubuffer, &apdulen) != 0) { }
  Serial.println("sale de while nfc.CardModeReceive...");
  
  Serial.println("nfc.CardModeReceive = ");
  Serial.println(nfc.CardModeReceive(apdubuffer, &apdulen));

  if (nfc.CardModeReceive(apdubuffer, &apdulen) == 1) { //0->1
if(debug) {
    printData(apdubuffer, apdulen, 4);
}
    Serial.println("entró a publicar...");
    client.publish(outTopic, apdubuffer, apdulen);
    tiempo = millis();  // more time before close the connection
    }
  else {
    Serial.println("Error reading the card data");
    printData(apdubuffer, apdulen, 4);
    if(debug) {
      Serial.println("Error reading the card!");
    }
    client.publish(outTopic, "N");
    blink(L1, 300, 10);  
  }
}

//Is it a card in range? for Mifare and ISO cards
void detectcard() {
  int attempts = 0;
  while (detectCardFlag == 0) {
if(debug) {
    Serial.println("wait detect Card...");
}
    if (!nfc.WaitForDiscoveryNotification(&RfInterface, 5000)) { // Waiting to detect cards

      if (RfInterface.ModeTech == MODE_POLL || RfInterface.ModeTech == TECH_PASSIVE_NFCA) {
        char tmp[16];
if(debug) {
        Serial.print("\tSENS_RES = ");
        sprintf(tmp, "0x%.2X", RfInterface.Info.NFC_APP.SensRes[0]);
        Serial.print(tmp); Serial.print(" ");
        sprintf(tmp, "0x%.2X", RfInterface.Info.NFC_APP.SensRes[1]);
        Serial.print(tmp); Serial.println(" ");
        Serial.print("\tNFCID = ");
        printBuf(RfInterface.Info.NFC_APP.NfcId, RfInterface.Info.NFC_APP.NfcIdLen);
}

        if (RfInterface.Info.NFC_APP.SelResLen != 0) {
if(debug) {
          Serial.print("\tSEL_RES = ");
          sprintf(tmp, "0x%.2X", RfInterface.Info.NFC_APP.SelRes[0]);
          Serial.print(tmp); Serial.println(" ");
}
        }
      }
      switch (RfInterface.Protocol) {
        case PROT_ISODEP:

if(debug) {
          Serial.println(" - Found ISODEP card");
}

          seekTrack2();
          break;

        case PROT_MIFARE:
          if(debug) {
            Serial.println(" - Found MIFARE card");
          }  
          break;

        default:
          attempts++;
          if (attempts > 4) {
            client.publish(outTopic, "N");
            return;
          }
          if(debug) {
           Serial.println(" - Not a valid card");
          } 
          blink(L1, 50, 20);
          break;
      }
      detectCardFlag = 1;
    }
    else {
      if(debug) {
        Serial.println("No Detect");
      }
      blink(L1, 50, 20);  

      attempts++;
      if (attempts > 4) {
        client.publish(outTopic, "N");
        return;
      }
      blink(L1, 50, 20);

      dhost[4] = inTopic[11];
      dhost[5] = inTopic[12];
      dhost[1] = nhost / 10 + 48;
      dhost[2] = nhost % 10 + 48;

      client.publish("queue", dhost);

      dhost[4] = '#';
      dhost[5] = '#';
      dhost[1] = '#';
      dhost[2] = '#';
    }
  }
}

//To read Mifare and Visa
void mifarevisa() {

  if (detectCardFlag == 0) {
    mode = 1;
    resetMode();
    detectcard();
  }
  else {
    seekTrack2();
  }
}

void set_debug() {
  
  char *arg;
  arg = SCmd.next();    // Get the next argument from the SerialCommand object buffer
  if (arg != NULL) {
    if(strcmp(arg, "debug") == 0) {
      debug = 1;
      Serial.println("Debugging active");
    }
    else if(strcmp(arg, "nodebug") == 0) {
          debug = 0;
          Serial.println("OK");
    }
    else  
      Serial.println("ERROR");    
  }
  else {
    if(debug) {
      Serial.println("No argument");
    }  
    Serial.println("ERROR");
  }  
  
}

void set_n(){

  //nclient = CLIENT;
  
  char *arg;
  arg = SCmd.next();    // Get the next argument from the SerialCommand object buffer
  if (arg != NULL) { // && !rf) {    // As long as it existed, take it
  
    nhost = atoi(arg);

    if (nhost < 0 || nhost >= HMAX) {
      
      if(debug) {
        Serial.print("Error setting the host number must be between 0-");
      Serial.println(HMAX);
      }
      
      Serial.println("ERROR");
      return;
    }    
 
    if(debug) {   
      Serial.print("Host: ");
      Serial.println(nhost);
    }
      
    flagStore = 0;
  }
  else {
    if(debug) {
      Serial.println("No arguments for host number, get value from store");
    } 
    flagStore = 1; 
    result = getSketchStats(statsKey, &previousStats);
    nhost = previousStats.nhostStore;
  }
  
  if(debug) {
    Serial.print("Host: ");
    Serial.println(nhost);
  }     

  if (flagStore == 0) {
    result = getSketchStats(statsKey, &previousStats);

    previousStats.nhostStore = nhost;


    result = setSketchStats(statsKey, previousStats);

    if (result == MBED_SUCCESS) {
      
      if(debug) {
        Serial.println("Save configuration in flash");
        Serial.print("\tSSID: ");
        Serial.println(previousStats.ssidStore);
        Serial.print("\tWiFiPass: ");
        Serial.println(previousStats.passwordStore);
        Serial.print("\tMQTT Server: ");
        Serial.println(previousStats.mqttStore);
        Serial.print("\tClient: ");
        Serial.println(previousStats.nhostStore);
      }
        

    } else {
      if(debug) {
        Serial.println("Error while saving to key-value store");
      }
      Serial.println("ERROR");  
      while (1);
    }
  }
  
  Serial.println("OK"); 

   hostId[18] = nhost / 10 + 48;
   hostId[19] = nhost % 10 + 48;
   
   outTopic[9] = nhost / 10 + 48;
   outTopic[10] = nhost % 10 + 48;  

   dhost[4] = inTopic[11];
   dhost[5] = inTopic[12];
   dhost[1] = nhost / 10 + 48;
   dhost[2] = nhost % 10 + 48;
   client.publish("queue", dhost);

   dhost[4] = '#';
   dhost[5] = '#';
   dhost[1] = '#';
   dhost[2] = '#';  
}

/*****************
       WIFI
 *****************/
void setup_wifi() {
  char *arg;
  arg = SCmd.next();    // Get the next argument from the SerialCommand object buffer
  if (arg != NULL) {
    strcpy(ssid, arg);
    if(debug) {
      Serial.print("First argument was: ");
      Serial.println(ssid);
    }
    Serial.println("OK");  
  }
  else {
    if(debug) {
      Serial.println("No arguments for WiFi");
    }
    Serial.println("ERROR");  
  }

  arg = SCmd.next();
  if (arg != NULL) {
    strcpy(pass, arg);
    if(debug) {
      Serial.print("Second argument was: ");
      Serial.println(pass);
    }
    flagStore = 0;
  }
  else {
    if(debug) {
      Serial.println("No second argument for pass, get value from store");
    }
    result = getSketchStats(statsKey, &previousStats);

    strcpy(ssid, previousStats.ssidStore);
    strcpy(pass, previousStats.passwordStore);
  }

  // We start by connecting to a WiFi network
  if(debug) {
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);
  }  

  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    if(debug) {
      Serial.println("Communication with WiFi module failed!");
    }  
    // don't continue
    while (1);
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    if(debug) {
      Serial.println("Please upgrade the firmware");
    }  
  }
  int cont = 0;
  // attempt to connect to WiFi network:
  while (status != WL_CONNECTED) {
    if(debug) {
      Serial.print("Attempting to connect to SSID: ");
      Serial.println(ssid);
    }  
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    status = WiFi.begin(ssid, pass);
    if (status == WL_CONNECTED) {
      flagWifi = 1;
    }
    cont++;
    if (cont > 2) {
      break;
    }
  }
  if(debug) {
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }  
  Serial.println("OK");
  if (!flagStore) {
    result = getSketchStats(statsKey, &previousStats);

    strcpy(previousStats.ssidStore, ssid);
    strcpy(previousStats.passwordStore, pass);

    result = setSketchStats(statsKey, previousStats);

    if (result == MBED_SUCCESS) {
      if(debug) {
        Serial.println("Save WiFiSetup in Stats Flash");
        Serial.print("\tSSID: ");
        Serial.println(previousStats.ssidStore);
        Serial.print("\tWiFiPass: ");
        Serial.println(previousStats.passwordStore);
        Serial.print("\tMQTT Server: ");
        Serial.println(previousStats.mqttStore);
        Serial.print("\tID: ");
        Serial.println(previousStats.nhostStore);
      }  
      Serial.println("OK");

    } else {
      if(debug) {
        Serial.println("Error while saving to key-value store");
      }
      while (1);
    }
  }

}

/*****************
       MQTT
 *****************/

void setup_mqtt() {
  char *arg;
  arg = SCmd.next();    // Get the next argument from the SerialCommand object buffer
  if (arg != NULL) {    // As long as it existed, take it
    strcpy(mqtt_server, arg);
    if(debug) {
      Serial.print("MQTT Server: ");
      Serial.println(mqtt_server);
    }  
    flagStore = 0;
  }
  else {
    if(debug) {
      Serial.println("No arguments for MQTTServer, get value from store");
    }  
    result = getSketchStats(statsKey, &previousStats);
    strcpy(mqtt_server, previousStats.mqttStore);
  }
  if(debug) {
    Serial.print("Connecting MQTT to ");
    Serial.println(mqtt_server);
  }  
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  Serial.println("OK");

  if (!flagStore) {
    result = getSketchStats(statsKey, &previousStats);

    strcpy(previousStats.mqttStore, mqtt_server);


    result = setSketchStats(statsKey, previousStats);

    if (result == MBED_SUCCESS) {
      if(debug) {
        Serial.println("Save MQTTSetup in Stats Flash");
        Serial.print("\tSSID: ");
        Serial.println(previousStats.ssidStore);
        Serial.print("\tWiFiPass: ");
        Serial.println(previousStats.passwordStore);
        Serial.print("\tMQTT Server: ");
        Serial.println(previousStats.mqttStore);
      }  
      Serial.println("OK");
    } else {
      if(debug) {
        Serial.println("Error while saving to key-value store");
      }  
      while (1);
    }
  }
  flagMqtt = 1;
  if(debug) {
    Serial.println("connected MQTT");
  }  
}

//Callback MQTT suscribe to inTopic from RelayClient
void callback(char* topic, byte * payload, unsigned int length) {
//if(debug) {
  Serial.print("Host Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.println();
//}

  // update host status check if there is a client requesting
  if (strcmp(topic, "hosts") == 0) {
    if (payload[2 * nhost + 1] != '#') {    // the host is requested
      inTopic[11] = payload[2 * nhost];   // change client number
      inTopic[12] = payload[2 * nhost + 1];
      host_selected = 1;
      tiempo = millis();            // reset time
      client.subscribe(inTopic);
      //if(debug) {
        Serial.print("Suscribe Topic: ");
        Serial.println(inTopic);
      //}  
      return;
    }
  }

  if (strcmp(topic, inTopic) == 0 && host_selected == 1) {

    if (payload[0] == 'M' && length == 1) {
      if(debug) {
        Serial.println("Sending data magnetic card");
        Serial.println(inTopic);
      }  
      client.publish(outTopic, tracks);
      tiempo = -10000;
      return;
    }

    commandlarge = length;
    for (int i = 0; i < length; i++) {

      ppse[i] = payload[i];
if(debug) {
      Serial.print(payload[i], HEX);
}
    }
if(debug) {
    Serial.println();
}
    mifarevisa();
  }
}
// Connect and reconnect to MQTT
void reconnect() {
  int cont = 0;
  // Loop until we're reconnected
  while (!client.connected()) {
    if(debug) {
      Serial.print("Attempting MQTT connection...");
    }  
    // Attempt to connect
    hostId[18] = nhost / 10 + 48;
    hostId[19] = nhost % 10 + 48;
    if (client.connect(hostId)) {
      if(debug) {
        Serial.println(" connected");
      }  
      // Once connected, publish an announcement...
      buf[20] = nhost / 10 + 48;
      buf[21] = nhost % 10 + 48;
      client.publish("status", buf);
      // ... and resubscribe
      client.subscribe("hosts");
      if(debug) {
        Serial.println("Subscribed to the hosts topic");
      }  
    } else {
      cont++;
      if(debug) {
        Serial.print("failed, rc=");
        Serial.print(client.state());

        Serial.println(" try again in 1 seconds");
      }  
      // Wait 1 seconds before retrying
      delay(1000);
    }
    if (cont > 1) {
      flagMqtt = 0;
      break;
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
if(debug) {
  while (!Serial);
}
  resetMode();

  // Get limits of the the internal flash of the microcontroller
  auto [flashSize, startAddress, iapSize] = getFlashIAPLimits();
  if(debug) {
    Serial.print("Flash Size: ");
    Serial.print(flashSize / 1024.0 / 1024.0);
    Serial.println(" MB");
    Serial.print("FlashIAP Start Address: 0x");
    Serial.println(startAddress, HEX);
    Serial.print("FlashIAP Size: ");
    Serial.print(iapSize / 1024.0 / 1024.0);
    Serial.println(" MB");
  }  

  // Create a block device on the available space of the flash
  FlashIAPBlockDevice blockDevice(startAddress, iapSize);

  // Initialize the Flash IAP block device and print the memory layout
  blockDevice.init();

  // Initialize the key-value store
  if(debug) {
    Serial.print("Initializing TDBStore: ");
  }  
  auto result = store.init();
  if(debug) {
    Serial.println(result == MBED_SUCCESS ? "OK" : "Failed");
  }  
  if (result != MBED_SUCCESS)
    while (1); // Stop the sketch if an error occurs

  get_config();

  if (flagStore == 1) {
    set_n();
    setup_wifi();
  }
  if ((flagWifi == 1) && (flagStore == 1)) {
    set_n();
    setup_mqtt();
  }

  if (flagStore == 1) {
    result = getSketchStats(statsKey, &previousStats);
    strcpy(tracks, previousStats.trackStore);
  }

  outTopic[9] = nhost / 10 + 48;
  outTopic[10] = nhost % 10 + 48;
  if(debug) {
    Serial.println(outTopic);
  }  

  if (flagMqtt == 1) {
    reconnect();
  }
  //if(debug) {
    Serial.println("BomberCat, yes Sir!");
    Serial.println("Host Relay NFC");
    Serial.println("Welcome to the BomberCat CLI " + String(fwVersion, 1) + "v\n");
    Serial.println("Type help to get the available commands.");
    Serial.println("Electronic Cats ® 2022");
  //}
  
  // Setup callbacks for SerialCommand commands
  SCmd.addCommand("help", help);
  SCmd.addCommand("test_card", test_card);
  SCmd.addCommand("set_n", set_n);
  SCmd.addCommand("set_debug", set_debug);
  SCmd.addCommand("setup_wifi", setup_wifi);
  SCmd.addCommand("setup_mqtt", setup_mqtt);
  SCmd.addCommand("setup_track", setup_track);
  SCmd.addCommand("get_config", get_config);
  SCmd.setDefaultHandler(unrecognized);  // Handler for command that isn't matched  (says "What?")

  dhost[4] = inTopic[11];
  dhost[5] = inTopic[12];
  dhost[1] = nhost / 10 + 48;
  dhost[2] = nhost % 10 + 48;
  client.publish("queue", dhost);

  dhost[4] = '#';
  dhost[5] = '#';
  dhost[1] = '#';
  dhost[2] = '#';

  // blink to show we started up
  blink(L1, 200, 5);
}

void loop() { // Main loop
  
  if (flagMqtt == 1) {
    //MQTT Loop
    client.loop();
  }
  
  if (host_selected == 0) {
    //Serial.println("readSerial: ");
    SCmd.readSerial();
  }
  
  if ((millis() - tiempo) > PERIOD && host_selected == 1) {
    // Reset host connection
    host_selected = 0;
    detectCardFlag = 0;
    mode = 2;
    resetMode();
    client.unsubscribe(inTopic);

    dhost[4] = inTopic[11];
    dhost[5] = inTopic[12];
    dhost[1] = nhost / 10 + 48;
    dhost[2] = nhost % 10 + 48;
    client.publish("queue", dhost);

    dhost[4] = '#';
    dhost[5] = '#';
    dhost[1] = '#';
    dhost[2] = '#';
    
    //if(debug) {
      Serial.println("The host connection is terminated.");
    //}
    apdubuffer[0] = NULL;
    apdulen = 0;
    reconnect();
  }
}

void help() {
  Serial.println("Fw version: " + String(fwVersion, 1) + "v");
  Serial.println("\tConfiguration commands:");
  Serial.println("\tset_n");
  Serial.println("\tsetup_wifi");
  Serial.println("\tsetup_mqtt");
  Serial.println("\tsetup_track");

  Serial.println("Monitor commands:");
  Serial.println("\ttest_card");
  Serial.println("\tget_config");
  Serial.println("\tset_debug");
  Serial.println("..help");
}

void get_config() {
  Serial.println("\nBomberCat configurations: ");

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
    Serial.print("\tTracks: ");
    Serial.println(previousStats.trackStore);
    flagStore = 1;
  } else if (result == MBED_ERROR_ITEM_NOT_FOUND) {
    Serial.println("No previous data for wifi and mqtt was found.");
    Serial.println("Run setup_wifi command.");
    Serial.println("Run setup_mqtt command.");
    Serial.println("Run setup_track command.");
  } else {
    Serial.println("Error reading from key-value store.");
  }

  dhost[4] = inTopic[11];
  dhost[5] = inTopic[12];
  dhost[1] = nhost / 10 + 48;
  dhost[2] = nhost % 10 + 48;
  
  client.publish("queue", dhost);

  dhost[4] = '#';
  dhost[5] = '#';
  dhost[1] = '#';
  dhost[2] = '#';

  Serial.print("\tHost: ");
  Serial.println(outTopic);
  Serial.print("\tID: ");
  Serial.println(hostId);
  blink(L1, 300, 3);

}

void setup_track() {
  char *arg;
  arg = SCmd.next();     // Get the next argument from the SerialCommand object buffer
  if (arg != NULL) {     // As long as it existed, take it
    strcpy(tracks, arg); // Mod arg size in SCmd library for full tracks
    if(debug) {
      Serial.print("Tracks: ");
      Serial.println(tracks);
    }  
    flagStore = 0;
  }
  else {
    if(debug) {
      Serial.println("No arguments for Tracks");
    }  
    result = getSketchStats(statsKey, &previousStats);
    strcpy(tracks, previousStats.trackStore);
    Serial.println("OK");
  }

  if (!flagStore) {
    result = getSketchStats(statsKey, &previousStats);

    strcpy(previousStats.trackStore, tracks);


    result = setSketchStats(statsKey, previousStats);

    if (result == MBED_SUCCESS) {
      if(debug) {
        Serial.println("Save MQTTSetup in Stats Flash");
        Serial.print("\tSSID: ");
        Serial.println(previousStats.ssidStore);
        Serial.print("\tWiFiPass: ");
        Serial.println(previousStats.passwordStore);
        Serial.print("\tMQTT Server: ");
        Serial.println(previousStats.mqttStore);
        Serial.print("\tTracks: ");
        Serial.println(previousStats.trackStore);
      }  
      Serial.println("OK");
    } else {
      if(debug) {
        Serial.println("Error while saving to key-value store");
      }
      while (1);
    }
  }
}

void test_card() {
  mode = 1; //temporarily switch to mode 1 to test the card
  resetMode();
  Serial.println("Waiting for an Card ...");
  card(); //Waiting for card, if the card is correctly positioned returns OK
  mode = 2; //return to mode 2
  resetMode();
}

void displayCardInfo(RfIntf_t RfIntf){ //Funtion in charge to show the card/s in te field
  char tmp[16];
  while (1){
    switch(RfIntf.Protocol){  //Indetify card protocol
    case PROT_T1T:
    case PROT_T2T:
    case PROT_T3T:
    case PROT_ISODEP:
        Serial.print(" - POLL MODE: Remote activated tag type: ");
        Serial.println(RfIntf.Protocol);
        break;
    case PROT_ISO15693:
        Serial.println(" - POLL MODE: Remote ISO15693 card activated");
        break;
    case PROT_MIFARE:
        Serial.println(" - POLL MODE: Remote MIFARE card activated");
        break;
    default:
        Serial.println(" - POLL MODE: Undetermined target");
        return;
    }

    switch(RfIntf.ModeTech) { //Indetify card technology
      case (MODE_POLL | TECH_PASSIVE_NFCA):

          Serial.print("\tSENS_RES = ");
          sprintf(tmp, "0x%.2X",RfIntf.Info.NFC_APP.SensRes[0]);
          Serial.print(tmp); Serial.print(" ");
          sprintf(tmp, "0x%.2X",RfIntf.Info.NFC_APP.SensRes[1]);
          Serial.print(tmp); Serial.println(" ");
          
          Serial.print("\tNFCID = ");
          printBuf(RfIntf.Info.NFC_APP.NfcId, RfIntf.Info.NFC_APP.NfcIdLen);
          
          if(RfIntf.Info.NFC_APP.SelResLen != 0) {
              Serial.print("\tSEL_RES = ");
              sprintf(tmp, "0x%.2X",RfIntf.Info.NFC_APP.SelRes[0]);
              Serial.print(tmp); Serial.println(" ");
          }

      break;
  
      case (MODE_POLL | TECH_PASSIVE_NFCB):
          if(RfIntf.Info.NFC_BPP.SensResLen != 0) {
              Serial.print("\tSENS_RES = ");
              printBuf(RfIntf.Info.NFC_BPP.SensRes,RfIntf.Info.NFC_BPP.SensResLen);
          }
          break;
  
      case (MODE_POLL | TECH_PASSIVE_NFCF):
          Serial.print("\tBitrate = ");
          Serial.println((RfIntf.Info.NFC_FPP.BitRate == 1) ? "212" : "424");
          
          if(RfIntf.Info.NFC_FPP.SensResLen != 0) {
              Serial.print("\tSENS_RES = ");
              printBuf(RfIntf.Info.NFC_FPP.SensRes,RfIntf.Info.NFC_FPP.SensResLen);
          }
          break;
  
      case (MODE_POLL | TECH_PASSIVE_15693):
          Serial.print("\tID = ");
          printBuf(RfIntf.Info.NFC_VPP.ID,sizeof(RfIntf.Info.NFC_VPP.ID));
          
          Serial.print("\ntAFI = ");
          Serial.println(RfIntf.Info.NFC_VPP.AFI);
          
          Serial.print("\tDSFID = ");
          Serial.println(RfIntf.Info.NFC_VPP.DSFID,HEX);
      break;
  
      default:
          break;
    }
    if(RfIntf.MoreTags) { // It will try to identify more NFC cards if they are the same technology
      if(nfc.ReaderActivateNext(&RfIntf) == NFC_ERROR) break;
    }
    else break;
  }
}
  
void card() { 
  if(!nfc.WaitForDiscoveryNotification(&RfInterface)){ // Waiting to detect cards
    if(debug) {
      displayCardInfo(RfInterface);
    }
    Serial.println("OK");
    
    switch(RfInterface.Protocol) {
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
    if(RfInterface.MoreTags) {
        nfc.ReaderActivateNext(&RfInterface);
    }
    //* Wait for card removal 
//    nfc.ProcessReaderMode(RfInterface, PRESENCE_CHECK);
//    Serial.println("CARD REMOVED!");
    
    nfc.StopDiscovery();
    nfc.StartDiscovery(mode);
  }
  resetMode();
  //delay(500);  
}

// This gets set as the default handler, and gets called when no other command matches.
void unrecognized(const char *command) {
  if(debug) {
    Serial.println("Command not found, type help to get the valid commands");
  }
  Serial.println("ERROR");  
}
