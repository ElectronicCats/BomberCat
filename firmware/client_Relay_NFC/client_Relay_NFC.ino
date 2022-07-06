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

  Electronic Cats invests time and resources providing this open source code,
  please support Electronic Cats and open-source hardware by purchasing
  products from Electronic Cats!

  This code is beerware; if you see me (or any other Electronic Cats
  member) at the local, and you've found our code helpful,
  please buy us a round!
  Distributed as-is; no warranty is given.
  /************************************************************/

#include "arduino_secrets.h"
#include <ArduinoJson.h>
#include <SPI.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>
#include "Electroniccats_PN7150.h"

// Update these with values suitable for your network.

const char* ssid = ssidName;
const char* password = passWIFI;
const char* mqtt_server = mqttServ;

const char* outTopic = "RelayClient";
const char* inTopic = "RelayHost";

#define L1         (LED_BUILTIN)  //LED1 indicates activity
#define L2         (12)  //LED2 indicates the emulation process 
#define L3         (13)

#define NPIN       (2) //NFC Button

WiFiClient espClient;
int status = WL_IDLE_STATUS;

PubSubClient client(espClient);
unsigned long lastMsg = 0;

#define PN7150_IRQ   (7)
#define PN7150_VEN   (8)
#define PN7150_ADDR  (0x28)

#define KEY_MFC      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF        // Default Mifare Classic key

Electroniccats_PN7150 nfc(PN7150_IRQ, PN7150_VEN, PN7150_ADDR); // creates a global NFC device interface object, attached to pins 7 (IRQ) and 8 (VEN) and using the default I2C address 0x28
RfIntf_t RfInterface;

uint8_t mode = 2;                                                  // modes: 1 = Reader/ Writer, 2 = Emulation

boolean flag_send = false;
boolean flag_read = false;

unsigned char STATUSOK[] = {0x90, 0x00}, Cmd[256], CmdSize;

// Token = data to be use it as track 2
// 4412345605781234 = card number in this case
uint8_t token[19] = {0x44, 0x12, 0x34, 0x56, 0x05 , 0x78, 0x12, 0x34, 0xd1, 0x71, 0x12, 0x01, 0x00, 0x00, 0x03, 0x00, 0x00, 0x99, 0x1f};
uint8_t commandlarge = 0;
//Visa MSD emulation variables
uint8_t apdubuffer[255] = {}, apdulen;
uint8_t ppsea[255] = {};
uint8_t visaa[] = {0x6F, 0x1E, 0x84, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x03, 0x10, 0x10, 0xA5, 0x13, 0x50, 0x0B, 0x56, 0x49, 0x53, 0x41, 0x20, 0x43, 0x52, 0x45, 0x44, 0x49, 0x54, 0x9F, 0x38, 0x03, 0x9F, 0x66, 0x02, 0x90, 0x00};
uint8_t processinga[] = {0x80, 0x06, 0x00, 0x80, 0x08, 0x01, 0x01, 0x00, 0x90, 0x00};
uint8_t last [4] =  {0x70, 0x15, 0x57, 0x13};
uint8_t card[25] = {};
uint8_t statusapdu[2] = {0x90, 0x00};
uint8_t finished[] = {0x6f, 0x00};

uint8_t rapdu[] = {0x6F, 0x1E, 0x84, 0x0E, 0x31, 0x50, 0x41, 0x59, 0x2E, 0x53, 0x59, 0x53, 0x2E, 0x44, 0x44, 0x46, 0x30, 0x31, 0xA5, 0x0C, 0x88, 0x01, 0x01, 0x5F, 0x2D, 0x02, 0x65, 0x6E, 0x9F, 0x11, 0x01, 0x01, 0x90, 0x00};


boolean detectCardFlag = false;

uint8_t ppdol[255] = {0x80, 0xA8, 0x00, 0x00, 0x02, 0x83, 0x00};

void resetMode() { //Reset the configuration mode after each reading
  Serial.println("Reset...");
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

//Emulate a Visa MSD
void visamsd() {

  //Serial.println("Entro a visamsd");

  memcpy(&card[0], last, sizeof(last));
  memcpy(&card[4], token, sizeof(token));
  memcpy(&card[23], statusapdu, sizeof(statusapdu));

  uint8_t *apdus2[] = {ppsea, visaa, processinga, card, finished, finished};
  uint8_t apdusLen2 [] = { sizeof(ppsea), sizeof(visaa), sizeof(processinga), sizeof(card), sizeof(finished), sizeof(finished)};

  if (flag_send == true) {
    Serial.print("Send:");
    for (int i = 0; i < commandlarge; i++) {
      Serial.print(ppsea[i], HEX);
    }
    Serial.println();
    nfc.CardModeSend(ppsea, commandlarge);
    printData(ppsea, commandlarge, 3);

    flag_send = false;
    flag_read = false;
  }

  //for (uint8_t i = 0; i < 6; i++) {

  if (nfc.CardModeReceive(Cmd, &CmdSize) == 0) { //Data in buffer?

    while ((CmdSize < 2) && (Cmd[0] != 0x00)) {}

    printData(Cmd, CmdSize, 2);

    client.publish(outTopic, Cmd, CmdSize);
    flag_read = true;

  } /*else {
      i--;
    }
  }*/
}

void setup_wifi() {

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
    status = WiFi.begin(ssid, password);

    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }
}

void callback(char* topic, byte * payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  commandlarge = length;
  for (int i = 0; i < length; i++) {
    ppsea[i] = payload[i];
    Serial.print(payload[i], HEX);
  }
  Serial.println();
  flag_send = true;

  visamsd();
}
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "BomberCat-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("status", "Hello I'm here RelayClient");
      // ... and resubscribe
      client.subscribe(inTopic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  pinMode(L1, OUTPUT);
  pinMode(L2, OUTPUT);
  pinMode(L3, OUTPUT);
  pinMode(NPIN, INPUT_PULLUP);

  Serial.begin(9600);
  //while (!Serial);
  //mode = 2;
  resetMode();
  //delay(100);

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // blink to show we started up
  blink(L1, 300, 5);
  //blink(L2, 200, 2);
  //blink(L3, 200, 2);

  Serial.println("BomberCat, yes Sir!");
}

void blink(int pin, int msdelay, int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(msdelay);
    digitalWrite(pin, LOW);
    delay(msdelay);
  }
}


void loop() { // Main loop
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  if (flag_read == false) {
    visamsd();
  }
}
