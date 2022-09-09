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
#include <ArduinoJson.h>
#include <SPI.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>
#include <SerialCommand.h>
#include "Electroniccats_PN7150.h"

//#define DEBUG
#define SERIALCOMMAND_HARDWAREONLY
#define PERIOD 20000
#define CLIENT 0
 
SerialCommand SCmd;

float fwVersion= 0.1;

// Update these with values suitable for your network.

const char* ssid = ssidName;
const char* password = passWIFI;
const char* mqtt_server = mqttServ;

char outTopic[] = "RelayClient#"; //"RelayClient#";
char inTopic[] = "RelayHost#";

//const char* outTopic = "RelayClient";
//const char* inTopic = "RelayHost";

char buf[] = "Hello I'm here Client #";

boolean host_selected = false;
char hs[] = "##########"; // hosts status

unsigned long tiempo = 0;

// Create a random client ID
char clientId[] = "BomberCatClient-0#";

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

// consts get stored in ram as we don't adjust them
char tracks[128][2];

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

Electroniccats_PN7150 nfc(PN7150_IRQ, PN7150_VEN, PN7150_ADDR); // creates a global NFC device interface object, attached to pins 7 (IRQ) and 8 (VEN) and using the default I2C address 0x28
RfIntf_t RfInterface;

uint8_t mode = 2;                                                  // modes: 1 = Reader/ Writer, 2 = Emulation

boolean flag_send = false;
boolean flag_read = false;

unsigned char Cmd[256], CmdSize;

uint8_t commandlarge = 0;

//Visa MSD emulation variables
uint8_t apdubuffer[255] = {}, apdulen;
uint8_t ppsea[255] = {};

boolean detectCardFlag = false;

uint8_t ppdol[255] = {0x80, 0xA8, 0x00, 0x00, 0x02, 0x83, 0x00};

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
  int tmp, crc, lrc = 0;
  dir = 0;
  track--; // index 0

  // enable H-bridge and LED
  //digitalWrite(ENABLE_PIN, HIGH);

  // First put out a bunch of leading zeros.
  for (int i = 0; i < 25; i++)
    playBit(0);

  for (int i = 0; tracks[track][i] != '?'; i++)
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


  for (i = 0; tracks[track][i] != '?'; i++)
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
  //if (digitalRead(MPIN) == 0) {
    Serial.println("Activating MagSpoof...");
    playTrack(1 + (curTrack++ % 2));
    blink(L1, 150, 3);
    delay(400);
 // }
}

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

  if (flag_send == true) {
    #ifdef DEBUG
    Serial.print("Send:");
    for (int i = 0; i < commandlarge; i++) {
      Serial.print(ppsea[i], HEX);
    }
    Serial.println();
    #endif
    nfc.CardModeSend(ppsea, commandlarge);
    #ifdef DEBUG
    printData(ppsea, commandlarge, 3);
    #endif

    flag_send = false;
    flag_read = false;
  }

  if (nfc.CardModeReceive(Cmd, &CmdSize) == 0) { //Data in buffer?

    while ((CmdSize < 2) && (Cmd[0] != 0x00)) {}

// *****************************************************
// Aquí publico mis mensajes para que el host pueda verlos (el host debe estar suscrito a mi topico)
    client.publish(outTopic, Cmd, CmdSize);

    
    #ifdef DEBUG
    printData(Cmd, CmdSize, 2);
    #endif
    flag_read = true;
    tiempo = millis();
  }
}

/*****************
       WIFI
 *****************/
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

/*****************
       MQTT
 *****************/
void callback(char* topic, byte * payload, unsigned int length) {
  #ifdef DEBUG
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  #endif


  //se actualiza el estatus de hs
  if (strcmp(topic,"hosts") == 0) {

    for (int i = 0; i < 10; i++) {
      hs[i] = payload[i];
    }
    return;
  }

  if (strcmp(topic,inTopic) == 0) { // mensaje del host
    
    commandlarge = length;
    
    for (int i = 0; i < length; i++) {
      ppsea[i] = payload[i];
      #ifdef DEBUG
      Serial.print(payload[i], HEX);
      #endif
    }
    #ifdef DEBUG
    Serial.println();
    #endif
    
    flag_send = true;
  
    visamsd();
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection... ");
    // Attempt to connect
    clientId[17] = CLIENT + 48;
    if (client.connect(clientId)) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      //client.publish("status", "Hello I'm here Client 0");
      buf[22] = CLIENT + 48;
      client.publish("status", buf);      
      // ... and resubscribe
      
      //client.subscribe(inTopic);
      
      client.subscribe("hosts");
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

void clean(){
  for(int i = 0; i < 256; i++){
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

  resetMode();

  pinMode(L1, OUTPUT);
  pinMode(PIN_A, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  pinMode(NPIN, INPUT);

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  // blink to show we started up
  blink(L1, 300, 5);

  outTopic[11] = CLIENT + 48;
  Serial.println(outTopic);
  reconnect();

  Serial.println("BomberCat, yes Sir!");
  Serial.println("Client Relay NFC");
  Serial.println("Welcome to the BomberCat CLI " + String(fwVersion,1) + "v\n");
  Serial.println("Type help to get the available commands.");
  Serial.println("Electronic Cats ® 2022");

  // Setup callbacks for SerialCommand commands 
  SCmd.addCommand("help",help); 
  SCmd.addCommand("set_h",set_h);
  SCmd.addCommand("free_h",free_h);
  SCmd.addCommand("mode_nfc",mode_nfc);
  SCmd.addCommand("mode_ms",mode_mb);
  SCmd.addCommand("get_hs",get_hs);

  SCmd.setDefaultHandler(unrecognized);  // Handler for command that isn't matched  (says "What?") 
}

// Main loop
void loop() {
  
  // procesa comandos seriales
  SCmd.readSerial();
  
  // procesa mensajes MQTT
  client.loop();
  
  if((millis() - tiempo) > PERIOD && host_selected){
    // RESET host connection
    host_selected = false;   
    // *************************************
    // poner cero en el arreglo del host que termina la conexion
    hs[inTopic[9] - 48] = '#'; 
    inTopic[9] = '#';
    // actualizar hosts, poner un 0 en el host que acaba de terminar...
    client.publish("hosts", (char*)hs);
    boolean success;
    success = client.unsubscribe(inTopic);
    Serial.println("Unsubscribe succes: ");
    Serial.println(success);

    clean();
    Serial.println("The host connection is terminated.");
  }

  if (flag_read == false && host_selected) {
    visamsd();
  }

  if (!client.connected()) {
    reconnect();
  }

}

void help(){
  Serial.println("Fw version: " + String(fwVersion,1)+"v");
  Serial.println("\tConfiguration commands:");
  Serial.println("\tset_h");
  Serial.println("\tfree_h");
  Serial.println("\tmode_nfc");
  Serial.println("\tmode_mb");

  Serial.println("Monitor commands:");
  Serial.println("\tget_hs");
  Serial.println("..help");
}

// validar que no haya un host seleccionado

void set_h(){
  char *arg;  
  arg = SCmd.next();    // Get the next argument from the SerialCommand object buffer
  int host;
  host = atoi(arg);

  if(host_selected){
    Serial.println("Wait for the current process to finish");  
    return;
  }
  
  boolean success;
  
  if (arg != NULL){
      switch (host){
        case 0:
/*          //check for host, assign turn, etc.
          Serial.println(hs);
          if(hs[0] != '#'){           
            Serial.println("Busy host, try again later.");
            return;
          }
          */
          inTopic[9] = '0'; // topic host id
          Serial.println(inTopic);
          success = client.subscribe(inTopic); //reconnect();
          Serial.println("Subscribe to host 0 success: ");
          Serial.println(success);
          
          host_selected = true; 
          tiempo = millis();
          // es necesario obtener el valor de hs primero
          hs[0] = CLIENT + 48;
          client.publish("hosts", (char*)hs);
          Serial.println(inTopic);
          Serial.println("Host 0 ready");
          break;
        case 1:
        /*
          //check for host, assign turn, etc.
          Serial.println(hs);
          if(hs[1] != '#'){           
            Serial.println("Busy host, try again later.");
            return;
          }  */      
          inTopic[9] = '1'; // topic id
          success = client.subscribe(inTopic); //reconnect();
          Serial.println("Subscribe to host 1 success: ");
          Serial.println(success);
          
          host_selected = true;
          tiempo = millis();
          // es necesario obtener el valor de hs primero
          hs[1] = CLIENT + 48;
          client.publish("hosts", (char*)hs);
          Serial.println(inTopic);
          Serial.println("Host 1 ready");
          break;
 /*       case 2:
          //check for host, assign turn, etc.
          Serial.println(hs);

          if(hs[2] != '#'){           
            Serial.println("Busy host, try again later.");
            return;
          }        
          inTopic[9] = '2'; // topic id
          host_selected = true;
          tiempo = millis();
          // es necesario obtener el valor de hs primero
          hs[2] = CLIENT + 48;;         
          client.publish("hosts", (char*)hs);
          Serial.println(inTopic);
          Serial.println("Host 2 ready");
          break;
        case 3:
          //check for host, assign turn, etc.
          Serial.println(hs);

          if(hs[3] != '#'){           
            Serial.println("Busy host, try again later.");
            return;
          }        
          inTopic[9] = '3'; // topic id
          reconnect();
          host_selected = true;
          tiempo = millis();
          // es necesario obtener el valor de hs primero
          hs[3] = CLIENT + 48;         
          client.publish("hosts", (char*)hs);
          Serial.println(inTopic);
          Serial.println("Host 3 ready");
          break;
        case 4:
          //check for host, assign turn, etc.
          Serial.println(hs);

          if(hs[4] != '#'){           
            Serial.println("Busy host, try again later.");
            return;
          }                
          inTopic[9] = '4'; // topic id
          reconnect();
          host_selected = true;
          tiempo = millis();
          // es necesario obtener el valor de hs primero
          hs[4] = CLIENT + 48;       
          client.publish("hosts", (char*)hs);
          Serial.println(inTopic);
          Serial.println("Host 4 ready");
          break;
        case 5:
          //check for host, assign turn, etc.
          Serial.println(hs);

          if(hs[5] != '#'){           
            Serial.println("Busy host, try again later.");
            return;
          }                
          inTopic[9] = '5'; // topic id
          reconnect();
          host_selected = true;
          tiempo = millis();
          // es necesario obtener el valor de hs primero
          hs[5] = CLIENT + 48;          
          client.publish("hosts", (char*)hs);
          Serial.println(inTopic);
          Serial.println("Host 5 ready");
          break;
        case 6:
          //check for host, assign turn, etc.
          Serial.println(hs);

          if(hs[6] != '#'){           
            Serial.println("Busy host, try again later.");
            return;
          }                
          inTopic[9] = '6'; // topic id
          reconnect();
          host_selected = true;
          tiempo = millis();
          // es necesario obtener el valor de hs primero
          hs[6] = CLIENT + 48;         
          client.publish("hosts", (char*)hs);
          Serial.println(inTopic);
          Serial.println("Host 6 ready");
          break;
        case 7:
          //check for host, assign turn, etc.
          Serial.println(hs);

          if(hs[7] != '#'){           
            Serial.println("Busy host, try again later.");
            return;
          }                
          inTopic[9] = '7'; // topic id
          reconnect();
          host_selected = true;
          tiempo = millis();
          // es necesario obtener el valor de hs primero
          hs[7] = CLIENT + 48;         
          client.publish("hosts", (char*)hs);
          Serial.println(inTopic);
          Serial.println("Host 7 ready");
          break;
        case 8:
          //check for host, assign turn, etc.
          Serial.println(hs);

          if(hs[8] != '#'){           
            Serial.println("Busy host, try again later.");
            return;
          }                
          inTopic[9] = '8'; // topic id
          reconnect();
          host_selected = true;
          tiempo = millis();
          // es necesario obtener el valor de hs primero
          hs[8] = CLIENT + 48;          
          client.publish("hosts", (char*)hs);
          Serial.println(inTopic);
          Serial.println("Host 8 ready");
          break;
        case 9:
          //check for host, assign turn, etc.
          Serial.println(hs);

          if(hs[9] != '#'){           
            Serial.println("Busy host, try again later.");
            return;
          }                
          inTopic[9] = '9'; // topic id
          reconnect();
          host_selected = true;
          tiempo = millis();
          // es necesario obtener el valor de hs primero
          hs[9] = CLIENT + 48;          
          client.publish("hosts", (char*)hs);
          Serial.println(inTopic);
          Serial.println("Host 9 ready");
          break;
*/                    
        default:
          Serial.println("Error setting the host value must be between 0-9");
          break;
      }
  } 
  else {
    Serial.println("No argument"); 
  }
}

void free_h(){
  char *arg;  
  arg = SCmd.next();    // Get the next argument from the SerialCommand object buffer
  int host;
  host = atoi(arg);
  if (arg != NULL){
      switch (host){
        case 0:
          //inTopic[9] = '0';
          //reconnect();
          host_selected = false;
          inTopic[9] = '#';
          Serial.println("Host 1 free");
          break;
        case 1:
          //inTopic[9] = '1';
          //reconnect();
          host_selected = false;
          inTopic[9] = '#';
          Serial.println("Host 2 free");
          break;
 /*       case 2:
          LoRa.setSignalBandwidth(15.6E3);
          rx_status = false;
          Serial.println("Bandwidth set to 15.6 kHz");
          break;
*/
        default:
          Serial.println("Error setting the host value must be between 0-9");
          break;
      }
  } 
  else {
    Serial.println("No argument"); 
  }
}

void mode_nfc(){
  Serial.print("Mode NFC ");
  Serial.println("HELL");
}

void mode_mb(){
  Serial.print("Mode magnetic band ");
  Serial.println("HELL");
}

void get_hs(){
  Serial.print("Hosts status: ");
  Serial.println("HELL");
}

// This gets set as the default handler, and gets called when no other command matches. 
void unrecognized(const char *command) {
  Serial.println("Command not found, type help to get the valid commands"); 
}
