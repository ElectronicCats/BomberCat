/*
The changes to this code were minumum and were mainly in the Void callback.
*/

#include "arduino_secrets.h"
#include <ArduinoJson.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>

const char* ssid = ssidName;
const char* password = passWIFI;
const char* mqtt_server = mqttServ;

#define L1         (LED_BUILTIN)  // LED1 indicates activity
#define PIN_A      (6)            // MagSpoof-1  
#define PIN_B      (7)            // MagSpoof
#define NPIN       (5)            // Button
#define CLOCK_US   (500)

#define BETWEEN_ZERO (53) // 53 zeros between track1 and 2
#define TRACKS (2)
#define DEBUGCAT

unsigned long myTime1;
unsigned long myTime2;
unsigned long result;

char tracks[2][128];
char revTrack[41];

const int sublen[] = { 32, 48, 48 };
const int bitlen[] = { 7, 5, 5 };

unsigned int curTrack = 0;
int dir;

WiFiClient espClient;
int status = WL_IDLE_STATUS;
PubSubClient client(espClient);

unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE  (50)
char msg[MSG_BUFFER_SIZE];
int value = 0;

/*****************
       WIFI
 *****************/
void setup_wifi() {
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    while (true);
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }

  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    status = WiFi.begin(ssid, password);
    delay(5000);

    if (status == WL_CONNECTED) {
      Serial.println("");
      Serial.println("WiFi connected");
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());
    }
  }
}

/*
The changes were to restrict the messages sent through the Mag topic and that can activate the MagSpoof mode. Now the code
verify if the message has the form of the track1 and track2, if it is not the case, then it descards it.
*/

void callback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, "Mag") == 0) {

    // Converts the payload into a character array    
    char message[length + 1];
    strncpy(message, (char*)payload, length);
    message[length] = '\0';

    // Look these specific characters in the payload. 
    char* questionMark = strchr(message, '?');
    char* semicolon = strchr(message, ';');

    // Verify that it contains % at the beginning and ? at the end, as well as the position of ? and ;
    if (message[0] != '%' || message[length - 1] != '?' || questionMark == NULL || semicolon == NULL || questionMark >= semicolon) {
      Serial.println("Invalid message received in the Mag topic. Ignoring...");
      return;
    }

    // Track 1: from the beginning to '?'
    int track1Length = questionMark - message + 1;
    strncpy(tracks[0], message, track1Length);
    tracks[0][track1Length] = '\0';

    // Track 2: from ';' to the end, including ';'
    int track2Start = semicolon - message;
    int track2End = length - track2Start;
    strncpy(tracks[1], message + track2Start, track2End);
    tracks[1][track2End] = '\0';

    // Shows the tracks in the Serial monitor
    Serial.println();
    Serial.println("Track 1:");
    Serial.println(tracks[0]);
    Serial.println("Track 2:");
    Serial.println(tracks[1]);

    magspoof();
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESPClient-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.publish("Mag", "hello i'm here");
      client.subscribe("Mag");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  pinMode(PIN_A, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  pinMode(L1, OUTPUT);
  pinMode(NPIN, INPUT_PULLUP);
  pinMode(13, OUTPUT);

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  Serial.begin(9600);
  while (!Serial);

  blink(L1, 200, 2);
  Serial.println("Ready MQTT MagSpoof");
}

void blink(int pin, int msdelay, int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(msdelay);
    digitalWrite(pin, LOW);
    delay(msdelay);
  }
}

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

void reverseTrack(int track) {
  int i = 0;
  track--;
  dir = 0;

  while (revTrack[i++] != '?');
  i--;
  while (i--)
    for (int j = bitlen[track] - 1; j >= 0; j--)
      playBit((revTrack[i] >> j) & 1);
}

void playTrack(int track) {
  int tmp, crc, lrc = 0;
  dir = 0;
  track--;

  for (int i = 0; i < 25; i++)
    playBit(0);

  for (int i = 0; tracks[track][i] != '?'; i++) {
    crc = 1;
    tmp = tracks[track][i] - sublen[track];

    for (int j = 0; j < bitlen[track] - 1; j++) {
      crc ^= tmp & 1;
      lrc ^= (tmp & 1) << j;
      playBit(tmp & 1);
      tmp >>= 1;
    }
    playBit(crc);
  }

  tmp = lrc;
  crc = 1;
  for (int j = 0; j < bitlen[track] - 1; j++) {
    crc ^= tmp & 1;
    playBit(tmp & 1);
    tmp >>= 1;
  }
  playBit(crc);

  if (track == 0) {
    for (int i = 0; i < BETWEEN_ZERO; i++)
      playBit(0);
    reverseTrack(2);
  }

  for (int i = 0; i < 5 * 5; i++)
    playBit(0);

  digitalWrite(PIN_A, LOW);
  digitalWrite(PIN_B, LOW);
}

void storeRevTrack(int track) {
  int i, tmp, crc, lrc = 0;
  track--;
  dir = 0;

  for (i = 0; tracks[track][i] != '?'; i++) {
    crc = 1;
    tmp = tracks[track][i] - sublen[track];

    for (int j = 0; j < bitlen[track] - 1; j++) {
      crc ^= tmp & 1;
      lrc ^= (tmp & 1) << j;
      tmp & 1 ? (revTrack[i] |= 1 << j) : (revTrack[i] &= ~(1 << j));
      tmp >>= 1;
    }
    crc ? (revTrack[i] |= 1 << 4) : (revTrack[i] &= ~(1 << 4));
  }
  tmp = lrc;
  crc = 1;
  for (int j = 0; j < bitlen[track] - 1; j++) {
    crc ^= tmp & 1;

    
    tmp & 1 ? (revTrack[i] |= 1 << j) : (revTrack[i] &= ~(1 << j));
    tmp >>= 1;
  }
  crc ? (revTrack[i] |= 1 << 4) : (revTrack[i] &= ~(1 << 4));

  i++;
  revTrack[i] = '?';
}

void magspoof() {
  Serial.println("Activating MagSpoof...");
  playTrack(1 + (curTrack++ % 2));
  blink(L1, 150, 3);
  delay(400);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}
