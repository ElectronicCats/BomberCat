/**
 * Example detect tags and show their unique ID
 * Authors:
 *        Salvador Mendoza - @Netxing - salmg.net
 *        For Electronic Cats - electroniccats.com
 * March 2020
 * 
 * Updated by Francisco Torres, Raul Vargas - Electronic Cats - electroniccats.com
 * 
 * Jul 2025
 *
 * This code is beerware; if you see me (or any other collaborator
 * member) at the local, and you've found our code helpful,
 * please buy us a round!
 * Distributed as-is; no warranty is given.
 */

void clearTagValues() {
  emulateNFCFlag = false;
  pollMode = "Waiting for NFC tag";
  nfcID = "";
  sensRes = "";
  selRes = "";
  nfcDiscoverySuccess = false;
}

void resetMode() {  // Reset the configuration mode after each reading
  debug.println("Re-initializing...");

  if (nfc.connectNCI()) {  // Wake up the board
    debug.println("Error while setting up the mode, check connections!");
    while (1)
      ;
  }

  if (nfc.getMode() == 1) {
    if (nfc.configureSettings()) {
      debug.println("The Configure Settings is failed!");
      while (1)
        ;
    }
  } else if (nfc.getMode() == 2) {
    if (nfc.configureSettings(uidcf, uidlen)) {
      debug.println("The Configure Settings is failed!");
      while (1)
        ;
    }
  }

  // Read/Write mode as default
  if (nfc.configMode()) {  // Set up the configuration mode
    debug.println("The Configure Mode is failed!!");
    while (1)
      ;
  }
  nfc.startDiscovery();  // NCI Discovery mode
}

// TODO: replace PrintBuf by getHexRepresentation
void PrintBuf(const byte* data, const uint32_t numBytes) {  // Print hex data buffer in format
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

String getHexRepresentation(const byte* data, const uint32_t numBytes) {
  String hexString;
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
              
        debug.print("\tSENS_RES = ");
        sensRes = getHexRepresentation(
            nfc.remoteDevice.getSensRes(),
            nfc.remoteDevice.getSensResLen()
        );
        debug.println(sensRes);
        debug.println(" ");
      
        debug.print("\tNFCID = ");
        nfcID = getHexRepresentation(
            nfc.remoteDevice.getNFCID(),
            nfc.remoteDevice.getNFCIDLen()
        );
        debug.println(nfcID);
        debug.println(" ");

        // UIDCF
        uidcf[2] = 7 + nfc.remoteDevice.getNFCIDLen();
        uidcf[3] = 0x02;
        uidcf[8] = 0x33;
        uidcf[9] = nfc.remoteDevice.getNFCIDLen();

        uidlen = nfc.remoteDevice.getNFCIDLen();

        memcpy(&uidcf[10],
              nfc.remoteDevice.getNFCID(),
              nfc.remoteDevice.getNFCIDLen());

        debug.print("\tUIDCF: ");
        debug.println(getHexRepresentation(uidcf, uidlen + 10));

        // uidcf ready to fill CORE_CONF
        if (nfc.remoteDevice.getNFCIDLen() != 4) {
            debug.println("Ooops... this doesn't seem to be a Mifare Classic card!");
        }

        // SEL_RES
        if (nfc.remoteDevice.getSelResLen() != 0) {
            debug.print("\tSEL_RES = ");
            selRes = getHexRepresentation(
                nfc.remoteDevice.getSelRes(),
                nfc.remoteDevice.getSelResLen()
            );
            debug.println(selRes);
            debug.println(" ");
        }

        break;

      case (nfc.tech.PASSIVE_NFCB):
        if (nfc.remoteDevice.getSensResLen() != 0) {
            debug.print("\tSENS_RES = ");
            sensRes = getHexRepresentation(
                nfc.remoteDevice.getSensRes(),
                nfc.remoteDevice.getSensResLen()
            );
            debug.println(sensRes);
        }
        break;

      case (nfc.tech.PASSIVE_NFCF):
        // Bitrate
        debug.print("\tBitrate = ");
        bitRate = (nfc.remoteDevice.getBitRate() == 1) ? "212" : "424";
        debug.println(bitRate);
        debug.println(" ");

        // SENS_RES
        if (nfc.remoteDevice.getSensResLen() != 0) {
            debug.print("\tSENS_RES = ");
            sensRes = getHexRepresentation(
                nfc.remoteDevice.getSensRes(),
                nfc.remoteDevice.getSensResLen()
            );
            debug.println(sensRes);
            debug.println(" ");
        }
        break;

      case (nfc.tech.PASSIVE_NFCV):
        // ID
        nfcID = getHexRepresentation(
            nfc.remoteDevice.getID(),
            sizeof(nfc.remoteDevice.getID())
        );
        debug.print("\tID = ");
        debug.println(nfcID);

        // AFI
        afi = String(nfc.remoteDevice.getAFI());
        debug.print("\nAFI = ");
        debug.println(afi);

        // DSFID
        dsfid = String(nfc.remoteDevice.getDSFID(), HEX);
        debug.print("\tDSFID = ");
        debug.println(dsfid);

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

void setupNFC() {
  debug.println("Initializing NFC...");
  if (nfc.connectNCI()) {  // Wake up the board
    debug.println("Error while setting up the mode, check connections!");
    while (1)
      ;
  }

  if (nfc.configureSettings()) {
    debug.println("The Configure Settings is failed!");
    while (1)
      ;
  }

  if (nfc.configMode()) {  // Set up the configuration mode
    debug.println("The Configure Mode is failed!!");
    while (1)
      ;
  }
}

void detectTags() {
  if (nfc.isTagDetected()) { // Waiting to detect cards
    displayCardInfo();

    // It can detect multiple cards at the same time if they use the same protocol
    if (nfc.remoteDevice.hasMoreTags()) {
      nfc.activateNextTagDiscovery();
      debug.println("Multiple cards are detected!");
    }

    Serial.println("Remove the Card");
    nfc.waitForTagRemoval();
    debug.println("Card removed!");
  }

  debug.println("Restarting...");
  nfc.reset();
  debug.println("Waiting for a Card...");
  delay(500);
}
