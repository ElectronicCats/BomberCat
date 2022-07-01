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

const char* outTopic = "RelayHost";
const char* inTopic = "RelayClient";

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

unsigned char STATUSOK[] = {0x90, 0x00}, Cmd[256], CmdSize;

// Token = data to be use it as track 2
// 4412345605781234 = card number in this case
uint8_t token[19] = {0x44, 0x12, 0x34, 0x56, 0x05 , 0x78, 0x12, 0x34, 0xd1, 0x71, 0x12, 0x01, 0x00, 0x00, 0x03, 0x00, 0x00, 0x99, 0x1f};

//Visa MSD emulation variables
uint8_t apdubuffer[255] = {}, apdulen;
uint8_t ppsea[] = {0x6F, 0x23, 0x84, 0x0E, 0x32, 0x50, 0x41, 0x59, 0x2E, 0x53, 0x59, 0x53, 0x2E, 0x44, 0x44, 0x46, 0x30, 0x31, 0xA5, 0x11, 0xBF, 0x0C, 0x0E, 0x61, 0x0C, 0x4F, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x03, 0x10, 0x10, 0x87, 0x01, 0x01, 0x90, 0x00};
uint8_t visaa[] = {0x6F, 0x1E, 0x84, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x03, 0x10, 0x10, 0xA5, 0x13, 0x50, 0x0B, 0x56, 0x49, 0x53, 0x41, 0x20, 0x43, 0x52, 0x45, 0x44, 0x49, 0x54, 0x9F, 0x38, 0x03, 0x9F, 0x66, 0x02, 0x90, 0x00};
uint8_t processinga[] = {0x80, 0x06, 0x00, 0x80, 0x08, 0x01, 0x01, 0x00, 0x90, 0x00};
uint8_t last [4] =  {0x70, 0x15, 0x57, 0x13};
uint8_t card[25] = {};
uint8_t statusapdu[2] = {0x90, 0x00};
uint8_t finished[] = {0x6f, 0x00};

uint8_t ppse[] = {0x00, 0xA4, 0x04, 0x00, 0x0e, 0x32, 0x50, 0x41, 0x59, 0x2e, 0x53, 0x59, 0x53, 0x2e, 0x44, 0x44, 0x46, 0x30, 0x31, 0x00}; //20

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
/*
    treatPDOL function:
   Make a right format challenge using the card PDOL to extract more data(track 2)
   Note: This challenge only follows the format, do not use it as real challenge generator
*/
uint8_t treatPDOL(uint8_t* apdu) {
  uint8_t plen = 7;
  Serial.println("");
  //PDOL Format: 80 A8 00 00 + (Tamaño del PDOL+2) + 83 + Tamaño del PDOL + PDOL + 00
  for (uint8_t i = 1; i <= apdu[0]; i++) {
    if (apdu[i] == 0x9F && apdu[i + 1] == 0x66) {
      ppdol[plen] = 0xF6;
      ppdol[plen + 1] = 0x20;
      ppdol[plen + 2] = 0xC0;
      ppdol[plen + 3] = 0x00;
      plen += 4;
      i += 2;
    }
    else if (apdu[i] == 0x9F && apdu[i + 1] == 0x1A) {
      ppdol[plen] = 0x9F;
      ppdol[plen + 1] = 0x1A;
      plen += 2;
      i += 2;
    }
    else if (apdu[i] == 0x5F && apdu[i + 1] == 0x2A) {
      ppdol[plen] = 0x5F;
      ppdol[plen + 1] = 0x2A;
      plen += 2;
      i += 2;
    }
    else if (apdu[i] == 0x9A) {
      ppdol[plen] = 0x9A;
      ppdol[plen + 1] = 0x9A;
      ppdol[plen + 2] = 0x9A;
      plen += 3;
      i += 1;
    }
    else if (apdu[i] == 0x95) {
      ppdol[plen] = 0x95;
      ppdol[plen + 1] = 0x95;
      ppdol[plen + 2] = 0x95;
      ppdol[plen + 3] = 0x95;
      ppdol[plen + 4] = 0x95;
      plen += 5;
      i += 1;
    }
    else if (apdu[i] == 0x9C) {
      ppdol[plen] = 0x9C;
      plen += 1;
      i += 1;
    }
    else if (apdu[i] == 0x9F && apdu[i + 1] == 0x37) {
      ppdol[plen] = 0x9F;
      ppdol[plen + 1] = 0x37;
      ppdol[plen + 2] = 0x9F;
      ppdol[plen + 3] = 0x37;
      plen += 4;
      i += 2;
    }
    else {
      uint8_t u = apdu[i + 2];
      while (u > 0) {
        ppdol[plen] = 0;
        plen += 1;
        u--;
      }
      i += 2;
    }
  }
  ppdol[4] = (plen + 2) - 7; // Length of PDOL + 2
  ppdol[6] = plen - 7;       // Real length
  plen++;                    // +1 because the last 0
  ppdol[plen] = 0x00;        // Add the last 0 to the challenge
  return plen;
}

void printData(uint8_t* buff, uint8_t lenbuffer, uint8_t cmd) {
  char tmp[1];
  if (cmd == 1)
    Serial.print("\nCommand: ");
  else if (cmd == 2)
    Serial.print("\nReader command: ");
  else if (cmd == 3)
    Serial.print("\nHunter Cat answer: ");
  else
    Serial.print("\nCard answer: ");

  for (uint8_t u = 0; u < lenbuffer; u++) {
    sprintf(tmp, "0x%.2X", buff[u]);
    Serial.print(tmp); Serial.print(" ");
  }
}

//Find Track 2 in the NFC reading transaction
void seekTrack2() {
  //Serial.print("\n Init Full challenge: ");
  bool chktoken = false, existpdol = false;
  uint8_t apdubuffer[255] = {}, apdulen;

  
  uint8_t visa[] = {0x00, 0xA4, 0x04, 0x00, 0x07, 0xa0, 0x00, 0x00, 0x00, 0x03, 0x10, 0x10, 0x00}; //13
  uint8_t processing [] = {0x80, 0xA8, 0x00, 0x00, 0x02, 0x83, 0x00, 0x00}; //8
  uint8_t sfi[] = {0x00, 0xb2, 0x01, 0x0c, 0x00}; //5

  uint8_t *apdus[] = {ppse, visa, processing, sfi};
  uint8_t apdusLen [] = { sizeof(ppse), sizeof(visa), sizeof(processing), sizeof(sfi)};

  uint8_t pdol[50], plen = 8;
      //Serial.print("\nEnter For: ");
  //for (uint8_t i = 0; i < 1; i++) {
  uint8_t i = 0;
    //blink(L2, 150, 1);

    nfc.CardModeSend(apdus[i], apdusLen[i]);

    while (nfc.CardModeReceive(apdubuffer, &apdulen) != 0) { }

    if (nfc.CardModeReceive(apdubuffer, &apdulen) == 0) {
      printData(apdus[i], apdusLen[i], 1);
      printData(apdubuffer, apdulen, 4);
      client.publish(outTopic, apdubuffer, apdulen);
      for (uint8_t u = 0; u < apdulen; u++) {
        if (i == 1) {
          if (apdubuffer[u] == 0x9F && apdubuffer[u + 1] == 0x38) {
            for (uint8_t e = 0; e <= apdubuffer[u + 2]; e++)
              pdol[e] =  apdubuffer[u + e + 2];

            //plen = treatPDOL(pdol);
            apdus[2] = ppdol;
            apdusLen[2] = plen;
            existpdol = true;
          }
        }
        else if (i == 3) {
          if (apdubuffer[u] == 0x57 && apdubuffer[u + 1] == 0x13 && !chktoken) {
            chktoken = true;
            memcpy(&token, &apdubuffer[u + 2], 19);
            break;
          }
        }
      }
      /*if (i == 1) {
        char tmp[1];
        Serial.print("\nFull challenge: ");
        for (uint8_t b = 0; b < plen; b++) {
          sprintf(tmp, "0x%.2X", existpdol ? ppdol[b] : processing[b]);
          Serial.print(tmp); Serial.print(" ");
        }
        Serial.println("");
      }*/
      Serial.println("");
    }
    else{
      Serial.println("Error reading the card!");
    }

  //}
}

//Is it a card in range? for Mifare and ISO cards
void detectcard() {
  while (detectCardFlag == false) {
    Serial.println("wait detect Card...");
    if (!nfc.WaitForDiscoveryNotification(&RfInterface)) { // Waiting to detect cards

      if (RfInterface.ModeTech == MODE_POLL || RfInterface.ModeTech == TECH_PASSIVE_NFCA) {
        char tmp[16];

        Serial.print("\tSENS_RES = ");
        sprintf(tmp, "0x%.2X", RfInterface.Info.NFC_APP.SensRes[0]);
        Serial.print(tmp); Serial.print(" ");
        sprintf(tmp, "0x%.2X", RfInterface.Info.NFC_APP.SensRes[1]);
        Serial.print(tmp); Serial.println(" ");
        Serial.print("\tNFCID = ");
        printBuf(RfInterface.Info.NFC_APP.NfcId, RfInterface.Info.NFC_APP.NfcIdLen);

        if (RfInterface.Info.NFC_APP.NfcIdLen != 4) {

          Serial.println("Ooops ... this doesn't seem to be a Mifare Classic card!");
          return;
        }

        if (RfInterface.Info.NFC_APP.SelResLen != 0) {

          Serial.print("\tSEL_RES = ");
          sprintf(tmp, "0x%.2X", RfInterface.Info.NFC_APP.SelRes[0]);
          Serial.print(tmp); Serial.println(" ");
        }
      }
      switch (RfInterface.Protocol) {
        case PROT_ISODEP:

          Serial.println(" - Found ISODEP card");

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
      if (RfInterface.MoreTags) {
        nfc.ReaderActivateNext(&RfInterface);
      }

      //* Wait for card removal
      nfc.ProcessReaderMode(RfInterface, PRESENCE_CHECK);
      Serial.println("CARD REMOVED!");

      nfc.StopDiscovery();
      nfc.StartDiscovery(mode);
      detectCardFlag = true;
    }
  }
}

//To read Mifare and Visa
void mifarevisa() {
  mode = 1;
  resetMode();
  detectcard();
  detectCardFlag = false;
}

void setup_wifi() {

  delay(10);
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
  for (int i = 0; i < length; i++) {
    
    ppse[i] = payload[i];
    Serial.print((char)payload[i]);
  }
  Serial.println();
  mifarevisa();
  
  //Time test
  //  Serial.print("Time: ");
  //  myTime2 = millis();
  //  result = myTime1 - myTime2;
  //  Serial.println(result); // prints time since program started
}
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESPClient-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("status", "Hello I'm here RelayHost");
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
  while (!Serial);

  resetMode();

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // blink to show we started up
  blink(L1, 200, 2);
  blink(L2, 200, 2);
  blink(L3, 200, 2);
  
  Serial.println("BomberCat, yes Sir!");
  Serial.println("Host Relay NFC");
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
}
