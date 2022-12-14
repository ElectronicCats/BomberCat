/************************************************************
  Example for read NFC card via MQTT version for BomberCat
  by Andres Sabas, Electronic Cats (https://electroniccats.com/)
  by Raul Vargas
  by Salvador Mendoza (salmg.net)
  Date: 22/11/2022

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

************************************************************/

#include "arduino_secrets.h"
#include <SPI.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>
#include <SerialCommand.h>
#include "Electroniccats_PN7150.h"
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

//#define DEBUG
#define SERIALCOMMAND_HARDWAREONLY
#define PERIOD 10000
#define CLIENT 23

#define HMAX 42

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

char outTopic[] = "RelayClient##";
char inTopic[] = "RelayHost##";

char shost[] = "c##h##";
char dhost[] = "h##c##"; 

char buf[] = "Hello I'm here Client ##";

int host_selected = 0;
char hs[2*HMAX];// = "####################################################################################"; // hosts status 42 host max


int ms_ok = 0;
int once_time = 0;
int hostupdate = 0; //host update
int hostready = 0; //host ready
int rf = 0; //reconnect flag
int nhost;

unsigned long tiempo = 0;

// Create a random client ID
char clientId[] = "BomberCatClient-##";

#define L1         (LED_BUILTIN)  //LED1 indicates activity

#define PN7150_IRQ   (11)
#define PN7150_VEN   (13)
#define PN7150_ADDR  (0x28)

#define PIN_A      (6)   //MagSpoof-1  
#define PIN_B      (7)   //MagSpoof

#define NPIN       (5) //Button

#define CLOCK_US   (500)

#define BETWEEN_ZERO (53) // 53 zeros between track1 & 2

#define TRACKS (2)

int msflag = 0;

// consts get stored in ram as we don't adjust them
char tracks[2][128];

char revTrack[41];

const int sublen[] = {
  32, 48, 48
};

const int bitlen[] = {
  7, 5, 5
};

unsigned int curTrack = 0;
int dir;

WiFiClient espClient;
int status = WL_IDLE_STATUS;

PubSubClient client(espClient);
unsigned long lastMsg = 0;

int flagWifi, flagMqtt, flagStore = 0;

Electroniccats_PN7150 nfc(PN7150_IRQ, PN7150_VEN, PN7150_ADDR); // creates a global NFC device interface object, attached to pins 7 (IRQ) and 8 (VEN) and using the default I2C address 0x28
RfIntf_t RfInterface;

uint8_t mode = 2;                                                  // modes: 1 = Reader/ Writer, 2 = Emulation

int flag_send = 0;
int flag_read = 0;

unsigned char Cmd[256], CmdSize;

uint8_t commandlarge = 0;

//Visa MSD emulation variables
uint8_t apdubuffer[255] = {}, apdulen;
uint8_t ppsea[255] = {};

int detectCardFlag = 0;

uint8_t ppdol[255] = {0x80, 0xA8, 0x00, 0x00, 0x02, 0x83, 0x00};


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
       MAGSPOOF
 *****************/
// send a single bit out
void playBit(int sendBit) {
  dir ^= 1;
  digitalWrite(PIN_A, dir);
  digitalWrite(PIN_B, !dir);
  delayMicroseconds(CLOCK_US);

  if (sendBit) {
    dir ^= 1;
    digitalWrite(PIN_A, dir);
    digitalWrite(PIN_B, !dir);
  }
  delayMicroseconds(CLOCK_US);
}

// when reversing
void reverseTrack(int track) {
  int i = 0;
  track--; // index 0
  dir = 0;

  while (revTrack[i++] != '?');
  i--;
  while (i--)
    for (int j = bitlen[track] - 1; j >= 0; j--)
      playBit((revTrack[i] >> j) & 1);
}

// plays out a full track, calculating CRCs and LRC
void playTrack(int track) {
  int tmp = 0, crc = 0, lrc = 0;
  dir = 0;
  track--; // index 0

  // First put out a bunch of leading zeros.
  for (int i = 0; i < 25; i++)
    playBit(0);

  for (int i = 0; tracks[track][i] != '\0'; i++)
  {
    crc = 1;
    tmp = tracks[track][i] - sublen[track];

    for (int j = 0; j < bitlen[track] - 1; j++)
    {
      crc ^= tmp & 1;
      lrc ^= (tmp & 1) << j;
      playBit(tmp & 1);
      tmp >>= 1;
    }
    playBit(crc);
  }

  // finish calculating and send last "byte" (LRC)
  tmp = lrc;
  crc = 1;
  for (int j = 0; j < bitlen[track] - 1; j++)
  {
    crc ^= tmp & 1;
    playBit(tmp & 1);
    tmp >>= 1;
  }
  playBit(crc);

  // if track 1, play 2nd track in reverse (like swiping back?)
  if (track == 0)
  {
    // if track 1, also play track 2 in reverse
    // zeros in between
    for (int i = 0; i < BETWEEN_ZERO; i++)
      playBit(0);

    // send second track in reverse
    reverseTrack(2);
  }

  // finish with 0's
  for (int i = 0; i < 5 * 5; i++)
    playBit(0);

  digitalWrite(PIN_A, LOW);
  digitalWrite(PIN_B, LOW);

}

// stores track for reverse usage later
void storeRevTrack(int track) {
  int i, tmp, crc, lrc = 0;
  track--; // index 0
  dir = 0;


  for (i = 0; tracks[track][i] != '\0'; i++)
  {
    crc = 1;
    tmp = tracks[track][i] - sublen[track];

    for (int j = 0; j < bitlen[track] - 1; j++)
    {
      crc ^= tmp & 1;
      lrc ^= (tmp & 1) << j;
      tmp & 1 ?
      (revTrack[i] |= 1 << j) :
      (revTrack[i] &= ~(1 << j));
      tmp >>= 1;
    }
    crc ?
    (revTrack[i] |= 1 << 4) :
    (revTrack[i] &= ~(1 << 4));
  }

  // finish calculating and send last "byte" (LRC)
  tmp = lrc;
  crc = 1;
  for (int j = 0; j < bitlen[track] - 1; j++)
  {
    crc ^= tmp & 1;
    tmp & 1 ?
    (revTrack[i] |= 1 << j) :
    (revTrack[i] &= ~(1 << j));
    tmp >>= 1;
  }
  crc ?
  (revTrack[i] |= 1 << 4) :
  (revTrack[i] &= ~(1 << 4));

  i++;
  revTrack[i] = '?';
}

void magspoof() {
  #ifdef DEBUG
    Serial.println("Activating MagSpoof...");
  #endif
  
  playTrack(1 + (curTrack++ % 2));
  blink(L1, 150, 3);
  tiempo = -10000;
}

void resetMode() { //Reset the configuration mode after each reading
  #ifdef DEBUG
    Serial.println("Reset...");
  #endif
  
  if (nfc.connectNCI()) { //Wake up the board
    #ifdef DEBUG
      Serial.println("Error while setting up the mode, check connections!");
    #endif
    Serial.println("ERROR");
    while (1);
  }

  if (nfc.ConfigureSettings()) {
    #ifdef DEBUG
      Serial.println("The Configure Settings failed!");
    #endif
    Serial.println("ERROR");
    while (1);
  }

  if (nfc.ConfigMode(mode)) { //Set up the configuration mode
    #ifdef DEBUG
      Serial.println("The Configure Mode failed!!");
    #endif
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

//Emulate a Visa MSD
void visamsd() {

  if (flag_send == 1) {

#ifdef DEBUG
    Serial.print("Send:");
    for (int i = 0; i < commandlarge; i++) {
      Serial.print(ppsea[i], HEX);
    }
    Serial.println();
#endif

    delay(100);
    nfc.CardModeSend(ppsea, commandlarge);

#ifdef DEBUG
    printData(ppsea, commandlarge, 3);
#endif

    flag_send = 0;
    flag_read = 0;
  }

  if (nfc.CardModeReceive(Cmd, &CmdSize) == 0) { //Data in buffer?

    while ((CmdSize < 2) && (Cmd[0] != 0x00)) {}

    // *****************************************************
    if (flag_send == 0) {
      delay(1500);
    }
    // Publish messages for host (the host should be subscribed to the topic)
    client.publish(outTopic, Cmd, CmdSize);

#ifdef DEBUG
    printData(Cmd, CmdSize, 2);
#endif

    flag_read = 1;
    tiempo = millis();
  }
  for (int i = 0; i < commandlarge; i++) {
    ppsea[i] = 0;
  }
}

/*****************
       WIFI
 *****************/
void setup_wifi() {
  char *arg;
  arg = SCmd.next();    // Get the next argument from the SerialCommand object buffer
  if (arg != NULL) {
    strcpy(ssid, arg);
    
    #ifdef DEBUG
      Serial.print("First argument was: ");
    #endif
      
    Serial.println(ssid);
  }
  else {
    #ifdef DEBUG
      Serial.println("No arguments for WiFi");
    #endif
    Serial.println("ERROR");  
  }

  arg = SCmd.next();
  if (arg != NULL) {
    strcpy(pass, arg);
    
    #ifdef DEBUG    
      Serial.print("Second argument was: ");
      Serial.println(pass);
    #endif
    
    flagStore = 0;
  }
  else {
    
    #ifdef DEBUG     
      Serial.println("No second argument for pass, get value from store");
    #endif
    flagStore = 1;  
    result = getSketchStats(statsKey, &previousStats);

    strcpy(ssid, previousStats.ssidStore);
    strcpy(pass, previousStats.passwordStore);
  }

  // We start by connecting to a WiFi network
    #ifdef DEBUG    
      Serial.println();
      Serial.print("Connecting to ");
      Serial.println(ssid);
    #endif 
    
  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    #ifdef DEBUG     
      Serial.println("Communication with WiFi module failed!");
    #endif
    Serial.println("ERROR");
      
    // don't continue
    while (1);
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    #ifdef DEBUG  
      Serial.println("Please upgrade the firmware");
    #endif  
  }

  int cont = 0;
  // attempt to connect to WiFi network:
  while (status != WL_CONNECTED) {
    
    #ifdef DEBUG
      Serial.print("Attempting to connect to SSID: ");
      Serial.println(ssid);
    #endif
      
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    status = WiFi.begin(ssid, pass);
    if(status == WL_CONNECTED){
      flagWifi = 1;
    }
    cont++;
    if (cont > 5) {
      break;
    }
  }

  #ifdef DEBUG
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  #endif
  
  if (flagStore != 0) Serial.println("OK");
    
  if (flagStore == 0) {
    result = getSketchStats(statsKey, &previousStats);

    strcpy(previousStats.ssidStore, ssid);
    strcpy(previousStats.passwordStore, pass);

    result = setSketchStats(statsKey, previousStats);

    if (result == MBED_SUCCESS) {

      #ifdef DEBUG
        Serial.println("Save WiFiSetup in Stats Flash");
        Serial.print("\tSSID: ");
        Serial.println(previousStats.ssidStore);
        Serial.print("\tWiFiPass: ");
        Serial.println(previousStats.passwordStore);
        Serial.print("\tMQTT Server: ");
        Serial.println(previousStats.mqttStore);
      #endif
      
        Serial.println("OK");

    } else {
      
      #ifdef DEBUG      
        Serial.println("Error while saving to key-value store");
      #endif
      Serial.println("ERROR");
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
  if (arg != NULL && !rf) {    // As long as it existed, take it
    strcpy(mqtt_server, arg);
    
    #ifdef DEBUG   
      Serial.print("MQTT Server: ");
      Serial.println(mqtt_server);
    #endif
      
    flagStore = 0;
  }
  else {
    #ifdef DEBUG
      Serial.println("No arguments for MQTTServer, get value from store");
    #endif 
    flagStore = 1; 
    result = getSketchStats(statsKey, &previousStats);
    strcpy(mqtt_server, previousStats.mqttStore);
  }
  #ifdef DEBUG
    Serial.print("Connecting MQTT to ");
    Serial.println(mqtt_server);
  #endif     
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);

  if (flagStore == 0) {
    result = getSketchStats(statsKey, &previousStats);

    strcpy(previousStats.mqttStore, mqtt_server);


    result = setSketchStats(statsKey, previousStats);

    if (result == MBED_SUCCESS) {
      
      #ifdef DEBUG
        Serial.println("Save MQTTSetup in Stats Flash");
        Serial.print("\tSSID: ");
        Serial.println(previousStats.ssidStore);
        Serial.print("\tWiFiPass: ");
        Serial.println(previousStats.passwordStore);
        Serial.print("\tMQTT Server: ");
        Serial.println(previousStats.mqttStore);
      #endif
        

    } else {
      #ifdef DEBUG
        Serial.println("Error while saving to key-value store");
      #endif
      Serial.println("ERROR");  
      while (1);
    }
  }
  flagMqtt = 1;
  #ifdef DEBUG
    Serial.println("connected MQTT");
  #endif
  
  Serial.println("OK"); 
  rf = 0;   
}


void printh(uint8_t n, uint8_t base)
{
  char buf[8 * sizeof(long) + 1]; // Assumes 8-bit chars plus zero byte.
  char *str = &buf[sizeof(buf) - 1];

  *str = '\0';

  // prevent crash if called with base == 1
  if (base < 2) base = 10;

  do {
    char c = n % base;
    n /= base;

    *--str = c < 10 ? c + '0' : c + 'A' - 10;
  } while(n);

  //Serial.write(str);
  delay(1);
}


void callback(char* topic, byte * payload, unsigned int length) {

#ifdef DEBUG
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
#endif


  //Update status Host
  if (strcmp(topic, "hosts") == 0) {

    for (int i = 0; i < 2*HMAX; i++) {
      hs[i] = payload[i];
    }
    
    hostupdate = 1;
    
    return;
  }

  if (strcmp(topic, inTopic) == 0) { // mensaje del host

    if (payload[0] == 'N' && length == 1) {
      
      #ifdef DEBUG
        Serial.println();
        Serial.println("****************");
        Serial.println("Card no detected");
        Serial.println("****************");
        Serial.println();
      #endif
        
      return;
    }

    // Read card data
    if (msflag == 1) {
      int i, j;
      j = 0;
           
#ifdef DEBUG
      Serial.print("Payload: ");
      Serial.println((char *)payload);
#endif

      for (i = 0; i < 255; i++) {
        if (payload[i] == '?' && j == 0) {
          tracks[0][i] = payload[i];
          j = i;
          tracks[0][i + 1] = NULL;
        }
        if (j == 0) {
          tracks[0][i] = payload[i];
        }
        else {
          tracks[1][i - j] = payload[i + 1];
          if (payload[i + 1] == '?') {
            tracks[1][i - j + 1] = NULL;
            break;
          }
        }
      }     
    
#ifdef DEBUG
      Serial.println("TRACKS");
      Serial.println((char *)tracks[0]);
      Serial.println((char *)tracks[1]);
#endif


      magspoof();
      return;
    }

    commandlarge = length;

    for (int i = 0; i < length; i++) {
      ppsea[i] = payload[i];

      printh(payload[i], HEX);
      
#ifdef DEBUG
      Serial.print(payload[i], HEX);     
#endif

    }
    
  
#ifdef DEBUG
    Serial.println();
#endif

    flag_send = 1;

    visamsd();
  }
}

void reconnect() {
  
  int cont = 0;
  rf = 1; // flag - setup_mqtt origin reconnect
  setup_mqtt();
  // Loop until we're reconnected
  while (!client.connected()) {
    
    #ifdef DEBUG
      Serial.print("Attempting MQTT connection... ");
    #endif
      
    // Attempt to connect
    clientId[16] = CLIENT/10 + 48;
    clientId[17] = CLIENT%10 + 48;
    if (client.connect(clientId)) {
      #ifdef DEBUG
        Serial.println("connected");
      #endif  
      // Once connected, publish an announcement...
      buf[22] = CLIENT/10 + 48;
      buf[23] = CLIENT%10 + 48;
      client.publish("status", buf);
      client.subscribe("hosts");
      
    } else {
      cont++;

      #ifdef DEBUG
        Serial.print("failed, rc=");
        Serial.print(client.state());
        Serial.println(" try again in 1 seconds");
      #endif
        
      // Wait 1 seconds before retrying
      delay(500);
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

void clean() {
  for (int i = 0; i < 256; i++) {
    Cmd[i] = 0;
    apdubuffer[i] = 0;
    ppsea[i] = 0;
  }
  Cmd[256] = 0;
  commandlarge = 0;
}

void setup() {

  Serial.begin(9600);
  
#ifdef DEBUG
  while (!Serial);
#endif

  pinMode(L1, OUTPUT);
  pinMode(PIN_A, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  pinMode(NPIN, INPUT);

  resetMode();

  // Get limits of the the internal flash of the microcontroller
  auto [flashSize, startAddress, iapSize] = getFlashIAPLimits();
  #ifdef DEBUG
    Serial.print("Flash Size: ");
    Serial.print(flashSize / 1024.0 / 1024.0);
    Serial.println(" MB");
    Serial.print("FlashIAP Start Address: 0x");
    Serial.println(startAddress, HEX);
    Serial.print("FlashIAP Size: ");
    Serial.print(iapSize / 1024.0 / 1024.0);
    Serial.println(" MB");
  #endif  

  // Create a block device on the available space of the flash
  FlashIAPBlockDevice blockDevice(startAddress, iapSize);

  // Initialize the Flash IAP block device and print the memory layout
  blockDevice.init();

  // Initialize the key-value store
  #ifdef DEBUG
    Serial.print("Initializing TDBStore: ");
  #endif
    
  auto result = store.init();
  #ifdef DEBUG
    Serial.println(result == MBED_SUCCESS ? "OK" : "Failed");
  #endif
    
  if (result != MBED_SUCCESS)
    while (1); // Stop the sketch if an error occurs

  get_config();

  if (flagStore == 1) {
    setup_wifi();
  }
  if ((flagWifi == 1) && (flagStore == 1)) {
    setup_mqtt();
  }

  // blink to show we started up
  blink(L1, 300, 5);

  outTopic[11] = CLIENT/10 + 48;
  outTopic[12] = CLIENT%10 + 48;

  #ifdef DEBUG
    Serial.println(outTopic);
  #endif  

  if (flagMqtt == 1) {
    reconnect();
  }

  #ifdef DEBUG
    Serial.println("BomberCat, yes Sir!");
    Serial.println("Client Relay NFC");
    Serial.println("Welcome to the BomberCat CLI " + String(fwVersion, 1) + "v\n");
    Serial.println("Type help to get the available commands.");
    Serial.println("Electronic Cats ® 2022");
  #endif
  
  // Setup callbacks for SerialCommand commands
  SCmd.addCommand("help", help);
  SCmd.addCommand("set_h", set_h);
  SCmd.addCommand("free_h", free_h);
  SCmd.addCommand("mode_nfc", mode_nfc);
  SCmd.addCommand("mode_ms", mode_ms);
  SCmd.addCommand("setup_wifi", setup_wifi);
  SCmd.addCommand("setup_mqtt", setup_mqtt);
  SCmd.addCommand("get_config", get_config);

  SCmd.setDefaultHandler(unrecognized);  // Handler for command that isn't matched  (says "What?")

  shost[1] = CLIENT/10 + 48; // publish on queue
  shost[2] = CLIENT%10 + 48;
  client.publish("queue", shost);
  
  dhost[4] = CLIENT/10 + 48;
  dhost[5] = CLIENT%10 + 48;
}

// Main loop
void loop() {

  SCmd.readSerial(); // Process Serial Commands 

  if ((millis() - tiempo) > PERIOD && host_selected == 1) {
    // RESET host connection
    client.subscribe("hosts");
    host_selected = 0;
    hostupdate = 0;
    hostready = 0;

    inTopic[9] = '#';
    inTopic[10] = '#';

    shost[1] = '#';
    shost[2] = '#';
    shost[4] = '#';
    shost[5] = '#';
    
    client.unsubscribe(inTopic);
    clean();
    once_time = 0;
    flag_read = 0;

    #ifdef DEBUG
      Serial.println("The host connection is terminated.");
    #endif  
  }

  if (flag_read == 0 && host_selected == 1 && msflag == 0) {
    visamsd();
  }

  if (msflag == 1 && host_selected == 1 && once_time == 1) {
    #ifdef DEBUG
      Serial.println("Wait a moment...");
    #endif
      
    delay(1500);
    // Publish magstripe get data MS from host
    client.publish(outTopic, "M");
    once_time = 0;
    hostupdate = 0;
    hostready = 0;
  }

  if (flagMqtt == 1) {
    if (!client.connected()) {
      reconnect();
    }
  }

  if (flagMqtt == 1) {
    // Loop MQTT
    checkhostsupdate();
    client.loop();
  }

}

void help() {
  if(host_selected == 1)
    return;
  Serial.println("Fw version: " + String(fwVersion, 1) + "v");
  Serial.println("\tConfiguration commands:");
  Serial.println("\tset_h");
  Serial.println("\tfree_h");
  Serial.println("\tmode_nfc");
  Serial.println("\tmode_ms");
  Serial.println("\tsetup_wifi");
  Serial.println("\tsetup_mqtt");

  Serial.println("Monitor commands:");
  Serial.println("\tget_config");
  Serial.println("..help");
}


void checkhostsupdate() { 
    if(hostupdate && hostready) {
      select_h(nhost);
      hostready = 0; 
    }    
}

void select_h(int host) {
  #ifdef DEBUG
    Serial.println(hs);
  #endif 
  
  if (hs[2*host+1] != '#') {
    #ifdef DEBUG
      Serial.println("Busy host, try again later.");
    #endif

    Serial.println("ERROR");  
    return;
  }
  
//  inTopic[9] = host/10 + 48; // topic host id
//  inTopic[10] = host%10 + 48;
  
  client.subscribe(inTopic);
  host_selected = 1;
  hostupdate = 0;
  tiempo = millis();
  
  hs[2*host] = CLIENT/10 + 48;
  hs[2*host+1] = CLIENT%10 + 48;
//
//  shost[1] = CLIENT/10 + 48; // to publish on queue
//  shost[2] = CLIENT%10 + 48;
//  
//  shost[4] = inTopic[9];
//  shost[5] = inTopic[10];
//  
//  client.publish("queue", shost);
  
  #ifdef DEBUG
    Serial.println(inTopic);
    Serial.print("Host ");
    Serial.print(host);
    Serial.println(" ready");
  #endif 
  
  Serial.println("OK"); 
}

void set_h() {
  if(host_selected == 1) {
    Serial.println("ERROR");
    return;  
  }
  
  char *arg;
  arg = SCmd.next();    // Get the next argument from the SerialCommand object buffer

  if (arg != NULL) {

    int host;
    host = atoi(arg);
  
    if (host_selected) {
      #ifdef DEBUG
        Serial.println("Wait for the current process to finish");
      #endif
      Serial.println("ERROR");   
      return;
    }
    
    if (host < 0 || host >= HMAX) {
      
      #ifdef DEBUG
        Serial.print("Error setting the host value must be between 0-");
      Serial.println(HMAX);
      #endif
      
      Serial.println("ERROR");
      return;
    }

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++         // request to cordinator
    inTopic[9] = host/10 + 48; // topic host id
    inTopic[10] = host%10 + 48;
  
    shost[1] = CLIENT/10 + 48; // to publish on queue
    shost[2] = CLIENT%10 + 48;
    
    shost[4] = inTopic[9];
    shost[5] = inTopic[10];
    
    client.publish("queue", shost);
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    
    once_time = 1;
    nhost =  host;
    hostready = 1;
    //select_h(host);
  }
  else {
    #ifdef DEBUG
      Serial.println("No argument");
    #endif
    Serial.println("ERROR");  
  }
}

void free_h() { 
  char *arg;
  arg = SCmd.next();    // Get the next argument from the SerialCommand object buffer

  if (arg != NULL) {

    int host;
    host = atoi(arg);

    if (host < 0 || host >= HMAX) {
      #ifdef DEBUG
        Serial.print("Error setting the host value must be between 0-");
        Serial.println(HMAX);
      #endif
      Serial.println("ERROR");  
      return;
    } 
  
    dhost[1] = host/10 + 48;
    dhost[2] = host%10 + 48;
    
    client.publish("queue", dhost);
  
    dhost[1] = '#';
    dhost[2] = '#';

    Serial.println("OK");

  }
  else {
    #ifdef DEBUG
      Serial.println("No argument");
    #endif  
    Serial.println("ERROR");
  }  
}

void mode_nfc() {
  if(host_selected == 1)
    return;
  #ifdef DEBUG
    Serial.print("Mode NFC ");
  #endif
    
  msflag = 0;
  Serial.println("OK");
}

void mode_ms() {
  if(host_selected == 1)
    return;  
  #ifdef DEBUG
    Serial.println("\nMode magnetic stripe ");
  #endif
    
  msflag = 1;
  Serial.println("OK");
}

void get_config() {
  if(host_selected == 1)
    return;
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
    flagStore = 1;
  } else if (result == MBED_ERROR_ITEM_NOT_FOUND) {
    Serial.println("No previous data for wifi and mqtt was found.");
    Serial.println("Run setup_wifi command.");
    Serial.println("Run setup_mqtt command.");
  } else {
    Serial.println("Error reading from key-value store.");
  }

  Serial.print("\tRelay: ");
  Serial.println(outTopic);
  Serial.print("\tID: ");
  Serial.println(clientId);
  Serial.print("\tMode: ");
  Serial.println(msflag ? "Magnetic Strip" : "NFC");
  Serial.println("OK");

}

// This gets set as the default handler, and gets called when no other command matches.
void unrecognized(const char *command) {
  #ifdef DEBUG
    Serial.println("Command not found, type help to get the valid commands");
  #endif
  Serial.println("ERROR");
}
