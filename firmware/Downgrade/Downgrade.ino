#include <Wire.h>
#include "Electroniccats_PN7150.h"

// Definition of pins and constants
#define PN7150_IRQ   (11)
#define PN7150_VEN   (13)
#define PN7150_ADDR  (0x28) // I2C address

#define L1 (LED_BUILTIN) // Built-in LED pin
#define PIN_A (6) // MagSpoof pin output A
#define PIN_B (7) // MagSpoof pin output B

#define NPIN (5)  // NFC Button pin
#define CLOCK_US (500) // Clock duration in microseconds
#define BETWEEN_ZERO (53) // Number of zeros between track 1 and 2

// Declaration of objects and global variables
Electroniccats_PN7150 nfc(PN7150_IRQ, PN7150_VEN, PN7150_ADDR);

char token[19]; // Buffer to hold token data
String tokenString = ""; // Token data as string

uint8_t ppdol [255] = {0x80, 0xA8, 0x00, 0x00, 0x02, 0x83, 0x00}; // Processing options data object list
char revTrack[41]; // Buffer to hold reversed track data
const int sublen[] = {32, 48, 48}; // Subtraction lengths for track data
const int bitlen[] = {7, 5, 5}; // Bit lengths for track data
int dir; // Direction flag

// Function to blink an LED
void blink(int pin, int msdelay, int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(msdelay);
    digitalWrite(pin, LOW);
    delay(msdelay);
  }
}

void displayCardInfo() {  // Funtion in charge to show the card/s in te field
  char tmp[16];

  while (true) {
    switch (nfc.remoteDevice.getProtocol()) {  // Indetify card protocol
      case nfc.protocol.T1T:
      case nfc.protocol.T2T:
      case nfc.protocol.T3T:
      case nfc.protocol.ISODEP:
        Serial.print(" - POLL MODE: Remote activated tag type: ");
        Serial.println(nfc.remoteDevice.getProtocol());
        break;
      case nfc.protocol.ISO15693:
        Serial.println(" - POLL MODE: Remote ISO15693 card activated");
        break;
      case nfc.protocol.MIFARE:
        Serial.println(" - POLL MODE: Remote MIFARE card activated");
        break;
      default:
        Serial.println(" - POLL MODE: Undetermined target");
        return;
    }

    switch (nfc.remoteDevice.getModeTech()) {  // Indetify card technology
      case (nfc.tech.PASSIVE_NFCA):
        Serial.println("\tTechnology: NFC-A");
        Serial.print("\tSENS RES = ");
        Serial.println(getHexRepresentation(nfc.remoteDevice.getSensRes(), nfc.remoteDevice.getSensResLen()));

        Serial.print("\tNFC ID = ");
        Serial.println(getHexRepresentation(nfc.remoteDevice.getNFCID(), nfc.remoteDevice.getNFCIDLen()));

        Serial.print("\tSEL RES = ");
        Serial.println(getHexRepresentation(nfc.remoteDevice.getSelRes(), nfc.remoteDevice.getSelResLen()));

        break;

      case (nfc.tech.PASSIVE_NFCB):
        Serial.println("\tTechnology: NFC-B");
        Serial.print("\tSENS RES = ");
        Serial.println(getHexRepresentation(nfc.remoteDevice.getSensRes(), nfc.remoteDevice.getSensResLen()));

        Serial.println("\tAttrib RES = ");
        Serial.println(getHexRepresentation(nfc.remoteDevice.getAttribRes(), nfc.remoteDevice.getAttribResLen()));

        break;

      case (nfc.tech.PASSIVE_NFCF):
        Serial.println("\tTechnology: NFC-F");
        Serial.print("\tSENS RES = ");
        Serial.println(getHexRepresentation(nfc.remoteDevice.getSensRes(), nfc.remoteDevice.getSensResLen()));

        Serial.print("\tBitrate = ");
        Serial.println((nfc.remoteDevice.getBitRate() == 1) ? "212" : "424");

        break;

      case (nfc.tech.PASSIVE_NFCV):
        Serial.println("\tTechnology: NFC-V");
        Serial.print("\tID = ");
        Serial.println(getHexRepresentation(nfc.remoteDevice.getID(), sizeof(nfc.remoteDevice.getID())));

        Serial.print("\tAFI = ");
        Serial.println(nfc.remoteDevice.getAFI());

        Serial.print("\tDSF ID = ");
        Serial.println(nfc.remoteDevice.getDSFID(), HEX);
        break;

      default:
        break;
    }

    // It can detect multiple cards at the same time if they are the same technology
    if (nfc.remoteDevice.hasMoreTags()) {
      Serial.println("Multiple cards are detected!");
      if (!nfc.activateNextTagDiscovery()) {
        break;  // Can't activate next tag
      }
    } else {
      break;
    }
  }
}

String getHexRepresentation(const byte* data, const uint32_t numBytes) {
  String hexString;

  if (numBytes == 0) {
    hexString = "null";
  }

  for (uint32_t szPos = 0; szPos < numBytes; szPos++) {
    hexString += "0x";
    if (data[szPos] <= 0xF)
      hexString += "0";
    hexString += String(data[szPos] & 0xFF, HEX);
    if ((numBytes > 1) && (szPos != numBytes - 1)) {
      hexString += " ";
    }
  }
  return hexString;
}

// Process the PDOL (Processing options data object list)
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
    } else if (apdu[i] == 0x9F && apdu[i + 1] == 0x1A) {
      ppdol[plen] = 0x9F;
      ppdol[plen + 1] = 0x1A;
      plen += 2;
      i += 2;
    } else if (apdu[i] == 0x5F && apdu[i + 1] == 0x2A) {
      ppdol[plen] = 0x5F;
      ppdol[plen + 1] = 0x2A;
      plen += 2;
      i += 2;
    } else if (apdu[i] == 0x9A) {
      ppdol[plen] = 0x9A;
      ppdol[plen + 1] = 0x9A;
      ppdol[plen + 2] = 0x9A;
      plen += 3;
      i += 1;
    } else if (apdu[i] == 0x95) {
      ppdol[plen] = 0x95;
      ppdol[plen + 1] = 0x95;
      ppdol[plen + 2] = 0x95;
      ppdol[plen + 3] = 0x95;
      ppdol[plen + 4] = 0x95;
      plen += 5;
      i += 1;
    } else if (apdu[i] == 0x9C) {
      ppdol[plen] = 0x9C;
      plen += 1;
      i += 1;
    } else if (apdu[i] == 0x9F && apdu[i + 1] == 0x37) {
      ppdol[plen] = 0x9F;
      ppdol[plen + 1] = 0x37;
      ppdol[plen + 2] = 0x9F;
      ppdol[plen + 3] = 0x37;
      plen += 4;
      i += 2;
    } else {
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
  ppdol[6] = plen - 7;       // Actual length
  plen++;                    // +1 because of the last 0
  ppdol[plen] = 0x00;        // Add the last 0 to the challenge
  return plen;
}

// Seek the track 2 data on the card
void seekTrack2() {
  bool chktoken = false;
  uint8_t apdubuffer[255] = {}, apdulen;

  // PPSE command
  uint8_t ppse[] = {0x00, 0xA4, 0x04, 0x00, 0x0E, 0x32, 0x50, 0x41, 0x59, 0x2E, 0x53, 0x59, 0x53, 0x2E, 0x44, 0x44, 0x46, 0x30, 0x31, 0x00};

  // Generic AID for payment applications
  uint8_t genericAid[] = {0x00, 0xA4, 0x04, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x04, 0x10, 0x10, 0x00};

  // GPO command
  uint8_t gpo[] = {0x80, 0xA8, 0x00, 0x00, 0x02, 0x83, 0x00, 0x00};

  // Read Record command
  uint8_t readRecord[] = {0x00, 0xB2, 0x01, 0x0C, 0x00}; //Esto podría estar mal

  // Send PPSE command
  blink(L1, 150, 1);
  Serial.print("\nSending PPSE command: ");
  printData(ppse, sizeof(ppse), 1);

  if (nfc.readerTagCmd(ppse, sizeof(ppse), &apdubuffer[0], &apdulen)) {
    Serial.print("\nReceived response: ");
    printData(apdubuffer, apdulen, 4);

    if (apdubuffer[apdulen - 2] == 0x90 && apdubuffer[apdulen - 1] == 0x00) { //if command = 9000  the response was successfull
      Serial.println("PPSE command successful!");

      // Send Select Application command
      blink(L1, 150, 1);
      Serial.print("\nSending Select Application command: ");
      printData(genericAid, sizeof(genericAid), 1);

      if (nfc.readerTagCmd(genericAid, sizeof(genericAid), &apdubuffer[0], &apdulen)) {
        Serial.print("\nReceived response: ");
        printData(apdubuffer, apdulen, 4);

        if (apdubuffer[apdulen - 2] == 0x90 && apdubuffer[apdulen - 1] == 0x00) {
          Serial.println("Select Application command successful!");

          // Send GPO command
          blink(L1, 150, 1);
          Serial.print("\nSending GPO command: ");
          printData(gpo, sizeof(gpo), 1);

          if (nfc.readerTagCmd(gpo, sizeof(gpo), &apdubuffer[0], &apdulen)) {
            Serial.print("\nReceived response: ");
            printData(apdubuffer, apdulen, 4);

            if (apdubuffer[apdulen - 2] == 0x90 && apdubuffer[apdulen - 1] == 0x00) {
              Serial.println("GPO command successful!");

              // Send Read Record command
              blink(L1, 150, 1);
              Serial.print("\nSending Read Record command: ");
              printData(readRecord, sizeof(readRecord), 1);

              if (nfc.readerTagCmd(readRecord, sizeof(readRecord), &apdubuffer[0], &apdulen)) {
                Serial.print("\nReceived response: ");
                printData(apdubuffer, apdulen, 4);

                if (apdubuffer[apdulen - 2] == 0x90 && apdubuffer[apdulen - 1] == 0x00) {
                  Serial.println("Read Record command successful!");
                  chktoken = true;
                  memcpy(token, &apdubuffer[5], 19); // Extract Track 2 data
                } else {
                  Serial.print("Read Record command failed with status: ");
                  Serial.print(apdubuffer[apdulen - 2], HEX);
                  Serial.println(apdubuffer[apdulen - 1], HEX);
                }
              } else {
                Serial.println("Error sending Read Record command!");
              }
            } else {
              Serial.print("GPO command failed with status: ");
              Serial.print(apdubuffer[apdulen - 2], HEX);
              Serial.println(apdubuffer[apdulen - 1], HEX);
            }
          } else {
            Serial.println("Error sending GPO command!");
          }
        } else {
          Serial.print("Select Application command failed with status: ");
          Serial.print(apdubuffer[apdulen - 2], HEX);
          Serial.println(apdubuffer[apdulen - 1], HEX);
        }
      } else {
        Serial.println("Error sending Select Application command!");
      }
    } else {
      Serial.print("PPSE command failed with status: ");
      Serial.print(apdubuffer[apdulen - 2], HEX);
      Serial.println(apdubuffer[apdulen - 1], HEX);
    }
  } else {
    Serial.println("Error sending PPSE command!");
  }

  if (chktoken) {
    formatToken();
  } else {
    Serial.println("Could not find the track 2!");
  }
}
// Format the token data
void formatToken() { 
  uint8_t i2 = 0;
  for (int i = 0; i <= sizeof(token) * 2; i++) {
    switch(i) {
      case 0:
        tokenString = String(';');
        break;
      case 17:
        tokenString += String('=');
        break;
      case 38:
        tokenString += String('?');
        break;
      default:
        if (i % 2)
          tokenString += String(token[i2] / 0x10);
        else {
          tokenString += String(token[i2] % 0x10);
          i2 += 1;
        }
        break;
    }
  }
  if (tokenString.length() > 0) {
    Serial.print("\nToken obtained and formatted: ");
    Serial.println(tokenString);
  }
}

// Play a bit for magnetic stripe emulation
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

// Play the magnetic track data
void playTrack(int track) {
  int tmp, crc, lrc = 0;
  dir = 0;
  track--; // index 0

  // Put first a bunch of initial zeros.
  for (int i = 0;  i < 25; i++) {
    playBit(0);
  }

  for (int i = 0; tokenString.length() && i < tokenString.length(); i++) {
    crc = 1;
    tmp = tokenString[i];
    tmp -= sublen[track];

    for (int j = 0; j < bitlen[track] - 1; j++) {
      crc ^= tmp & 1;
      lrc ^= (tmp & 1) << j;
      playBit(tmp & 1);
      tmp >>= 1;
    }
    playBit(crc);
  }

  // Finish calculating and send the last "byte" (LRC)
  tmp = lrc;
  crc = 1;
  for (int j = 0; j < bitlen[track] - 1; j++) {
    crc ^= tmp & 1;
    playBit(tmp & 1);
    tmp >>= 1;
  }
  playBit(crc);

  // If it's track 1, play the second track in reverse (as if it was passing backward)
  if (track == 0) {
    for (int i = 0; i < BETWEEN_ZERO; i++) {
      playBit(0);
    }

    reverseTrack(2);
  }

  // Finish with zeros
  for (int i = 0; i < 5 * 5; i++) {
    playBit(0);
  }

  digitalWrite(PIN_A, LOW);
  digitalWrite(PIN_B, LOW);
}

// Reverse the magnetic track data for track 2
void reverseTrack(int track) {
  int i = 0;
  track--; // index 0
  dir = 0;

  while (revTrack[i++] != '\0');
  i--;
  while (i--)
    for (int j = bitlen[track] - 1; j >= 0; j--)
      playBit((revTrack[i] >> j) & 1);
}

// Store reversed track data
void storeRevTrack(int track) {
  int i, tmp, crc, lrc = 0;
  track--; // index 0
  dir = 0;

  for (i = 0; tokenString[i] != '\0'; i++) {
    crc = 1;
    tmp = tokenString[i] - sublen[track];

    for (int j = 0; j < bitlen[track] - 1; j++) {
      crc ^= tmp & 1;
      lrc ^= (tmp & 1) << j;
      tmp & 1 ? (revTrack[i] |= 1 << j) : (revTrack[i] &= ~(1 << j));
      tmp >>= 1;
    }
    crc ? (revTrack[i] |= 1 << 4) : (revTrack[i] &= ~(1 << 4));
  }

  // Finish calculating and send the last "byte" (LRC)
  tmp = lrc;
  crc = 1;
  for (int j = 0; j < bitlen[track] - 1; j++) {
    crc ^= tmp & 1;
    tmp & 1 ? (revTrack[i] |= 1 << j) : (revTrack[i] &= ~(1 << j));
    tmp >>= 1;
  }
  crc ? (revTrack[i] |= 1 << 4) : (revTrack[i] &= ~(1 << 4));

  i++;
  revTrack[i] = '\0';
}

// Print data to the serial monitor
void printData(uint8_t* buff, uint8_t lenbuffer, uint8_t cmd) {
  char tmp[5];
  if (cmd == 1) 
    Serial.print("\nBomberCat Command: ");
 /* else if (cmd == 2) 
    Serial.print("\nReader command: ");
  else if (cmd == 3) 
    Serial.print("\nBomberCat answer: "); */
  else  
    Serial.print("\nCard answer: ");
        
  for (uint8_t u = 0; u < lenbuffer; u++) {
    sprintf(tmp, "0x%.2X",buff[u]);
    Serial.print(tmp); Serial.print(" ");
  }
  Serial.println();
}

// Setup function to initialize the system
void setup() {
  pinMode(PIN_A, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  pinMode(L1, OUTPUT);
  pinMode(NPIN, INPUT_PULLUP);

  Serial.begin(9600);
  while (!Serial);

  Serial.println("Initializing...");
  if (nfc.connectNCI()) {
    Serial.println("Error: Could not initialize PN7150!");
    while (1);
  }

  if (nfc.configureSettings()) {
    Serial.println("The Configure Settings is failed!");
    while (1)
      ;
  }

  // Read/Write mode as default
  if (nfc.configMode()) {  // Set up the configuration mode
    Serial.println("The Configure Mode is failed!!");
    while (1)
      ;
  }

  Serial.println("BomberCat initialized successfully!");
  Serial.println("Waiting for a card...");

  blink(L1, 200, 6); 
}

unsigned long pressStartTime = 0;
bool buttonPressed = false;
const unsigned long holdDuration = 2000; // 500 ms

// Main loop function to continuously check for NFC cards and handle button presses
void loop() {
  if (digitalRead(NPIN) == LOW) {
    if (!buttonPressed) {
      buttonPressed = true;
      pressStartTime = millis();
    } else {
      if (millis() - pressStartTime > holdDuration) {
        if (token[0] != 0x0) { 
          blink(L1, 150, 7); 
          Serial.println("\nActivating MagSpoof...");
          playTrack(2); 
          blink(L1, 100, 3); 
        } else {
          blink(L1, 500, 1);
          blink(L1, 500, 1);
          Serial.println("\nError: No data available for MagSpoof!");

          nfc.stopDiscovery();
          nfc.startDiscovery();
          Serial.println("\nReinitializing...");
          Serial.println("Waiting for a card...");
        }
        delay(400);
        buttonPressed = false;
      }
    }
  } else {
    if (buttonPressed && (millis() - pressStartTime <= holdDuration)) {
      Serial.println("\nShort press detected, starting NFC reading...");
      if (nfcread()) {
      }
    }
    buttonPressed = false;
  }
}

// Function to read NFC cards
bool nfcread() {
  nfc.startDiscovery();
  bool success = nfc.isTagDetected(500);

  if (success) {
    Serial.println("\nReading card...");
    digitalWrite(L1, HIGH); // Turn on LED during NFC reading
    displayCardInfo();
    Serial.println("Remove the Card");
    nfc.waitForTagRemoval();
    Serial.println("Card removed!");
    Serial.println("Restarting...");
    nfc.reset();
    delay(500);
    seekTrack2();
  } else {
    Serial.println("\nNo tag detected.");
  }

  return success;
}
