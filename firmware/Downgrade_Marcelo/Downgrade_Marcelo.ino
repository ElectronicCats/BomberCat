#include "Electroniccats_PN7150.h"
#define PN7150_IRQ (11)
#define PN7150_VEN (13)
#define PN7150_ADDR (0x28)

#define L1 (LED_BUILTIN) // Built-in LED pin
#define PIN_A (6) // MagSpoof pin output A
#define PIN_B (7) // MagSpoof pin output B

#define NPIN (5)  // NFC Button pin
#define CLOCK_US (500) // Clock duration in microseconds
#define BETWEEN_ZERO (53) // Number of zeros between track 1 and 2

Electroniccats_PN7150 nfc(PN7150_IRQ, PN7150_VEN, PN7150_ADDR);  // creates a global NFC device interface object, attached to pins 7 (IRQ) and 8 (VEN) and using the default I2C address 0x28

// Function prototypes
String getHexRepresentation(const byte* data, const uint32_t numBytes);
void displayCardInfo();
void printData(uint8_t* buff, uint8_t bufLen);
void blink(int pin, int msdelay, int times);

unsigned long pressStartTime = 0;
bool buttonPressed = false;
const unsigned long holdDuration = 2000;

void setup() {
  
  pinMode(PIN_A, OUTPUT);
  pinMode(PIN_B, OUTPUT);
  pinMode(L1, OUTPUT);
  pinMode(NPIN, INPUT_PULLUP);
  
  Serial.begin(9600);
  while (!Serial);

  Serial.println("Downgrade Example");

  Serial.println("Initializing...");
  if (nfc.connectNCI()) {  // Wake up the board
    Serial.println("Error while setting up the mode, check connections!");
    while (1)
      ;
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
  nfc.startDiscovery();  // NCI Discovery mode
  Serial.println("BomberCat initialized successfully!");
}

void loop() {
  if (digitalRead(NPIN) == LOW) { // Button is pressed
    delay(50); // Simple debounce delay
    if (!buttonPressed) { // First time detecting the button press
      buttonPressed = true;
      pressStartTime = millis(); // Record the start time of the press
    }
  } else { // Button is released
    if (buttonPressed) { // If the button was previously pressed
      unsigned long pressDuration = millis() - pressStartTime; // Calculate how long the button was pressed

      if (pressDuration > holdDuration) { // Long press detected
        Serial.println("\nLong press detected, MagSpoof is not ready...");
      } else { // Short press detected
        Serial.println("\nShort press detected, starting NFC reading...");
        seekTrack2();
      }

      buttonPressed = false; // Reset the button state
    }
  }
}

void seekTrack2() {
  
  //Define all the commands usef to get the card information and their lengths
  uint8_t ppse[] = {0x00, 0xA4, 0x04, 0x00, 0x0E, 0x32, 0x50, 0x41, 0x59, 0x2E, 0x53, 0x59, 0x53, 0x2E, 0x44, 0x44, 0x46, 0x30, 0x31, 0x00};
  uint8_t ppseLen= sizeof(ppse) / sizeof(ppse[0]);

  uint8_t genericAid[] = {0x00, 0xA4, 0x04, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x04, 0x10, 0x10, 0x00};
  uint8_t genericAidLen= sizeof(genericAid) / sizeof(genericAid[0]);

  uint8_t gpo[] = {0x80, 0xA8, 0x00, 0x00, 0x02, 0x83, 0x00, 0x00};
  uint8_t gpoLen= sizeof(gpo) / sizeof(gpo[0]);

  uint8_t readRecord[] = {0x00, 0xB2, 0x01, 0x14, 0x00};
  uint8_t readRecordLen= sizeof(readRecord) / sizeof(readRecord[0]);

  //Define the buffer for the APDU response from the card
  uint8_t apduBuffer[255] = {}, apduLen;

  //Check that an actual card is present
  if (nfc.isTagDetected()) {
    displayCardInfo();
    Serial.println();

    //PPSE Command
    Serial.print("Sending PPSE command: ");
    printData(ppse,ppseLen);
    nfc.readerTagCmd(ppse, ppseLen, &apduBuffer[0], &apduLen);
    Serial.print("PPSE response: ");
    printData(apduBuffer,apduLen);
    Serial.print("APDU response code: ");
    Serial.print(apduBuffer[apduLen - 2], HEX);
    Serial.println(apduBuffer[apduLen - 1], HEX);
    Serial.println();

    Serial.print("Sending genericAid command: ");
    printData(genericAid,genericAidLen);
    nfc.readerTagCmd(genericAid, genericAidLen, &apduBuffer[0], &apduLen);
    Serial.print("genericAid response: ");
    printData(apduBuffer,apduLen);
    Serial.print("APDU response code: ");
    Serial.print(apduBuffer[apduLen - 2], HEX);
    Serial.println(apduBuffer[apduLen - 1], HEX);
    Serial.println();

    Serial.print("Sending gpo command: ");
    printData(gpo,gpoLen);
    nfc.readerTagCmd(gpo, gpoLen, &apduBuffer[0], &apduLen);
    Serial.print("gpo response: ");
    printData(apduBuffer,apduLen);
    Serial.print("APDU response code: ");
    Serial.print(apduBuffer[apduLen - 2], HEX);
    Serial.println(apduBuffer[apduLen - 1], HEX);
    Serial.println();

    Serial.print("Sending readRecord command: ");
    printData(readRecord,readRecordLen);
    nfc.readerTagCmd(readRecord, readRecordLen, &apduBuffer[0], &apduLen);
    Serial.print("readRecord response: ");
    printData(apduBuffer,apduLen);
    Serial.print("APDU response code: ");
    Serial.print(apduBuffer[apduLen - 2], HEX);
    Serial.println(apduBuffer[apduLen - 1], HEX);
    Serial.println();
    
    Serial.println("Remove the Card");
    nfc.waitForTagRemoval();
    Serial.println("Card removed!");
  }
  else
  {
    Serial.println("No card present");
    Serial.println("Restarting...");
    nfc.reset();
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

void printData(uint8_t* buff, uint8_t bufLen) {
  char tmp[5];
  for (int i = 0; i < bufLen; i++) {
    sprintf(tmp, "%.2X",buff[i]);
    //sprintf(tmp, "0x%.2X",buff[i]);
    Serial.print(tmp); Serial.print(" ");
  }
  Serial.println();
}

void blink(int pin, int msdelay, int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(msdelay);
    digitalWrite(pin, LOW);
    delay(msdelay);
  }
}
