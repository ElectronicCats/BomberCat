/**
 * Example detect tags and show their unique ID
 * Authors:
 *        Salvador Mendoza - @Netxing - salmg.net
 *        For Electronic Cats - electroniccats.com
 *
 *  March 2020
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
  debug.println("Mode: " + String(mode));

  if (nfc.connectNCI()) {  // Wake up the board
    debug.println("Error while setting up the mode, check connections!");
    while (1)
      ;
  }

  if (mode == 1) {
    if (nfc.ConfigureSettings()) {
      debug.println("The Configure Settings is failed!");
      while (1)
        ;
    }
  } else if (mode == 2) {
    if (nfc.ConfigureSettings(uidcf, uidlen)) {
      debug.println("The Configure Settings is failed!");
      while (1)
        ;
    }
  }

  if (nfc.ConfigMode(mode)) {  // Set up the configuration mode
    debug.println("The Configure Mode is failed!!");
    while (1)
      ;
  }

  nfc.StartDiscovery(mode);
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

void displayCardInfo(RfIntf_t RfIntf) {  // Funtion in charge to show the card/s in te field
  char tmp[16];
  while (1) {
    switch (RfIntf.Protocol) {  // Indetify card protocol
      case PROT_T1T:
      case PROT_T2T:
      case PROT_T3T:
      case PROT_ISODEP:
        pollMode = "POLL MODE: Remote activated tag type: " + String(RfIntf.Protocol);
        debug.println(" - ", pollMode);
        break;
      case PROT_ISO15693:
        pollMode = "POLL MODE: Remote ISO15693 card activated";
        debug.println(" - ", pollMode);
        break;
      case PROT_MIFARE:
        pollMode = "POLL MODE: Remote MIFARE card activated";
        debug.println(" - ", pollMode);
        break;
      default:
        pollMode = "POLL MODE: Undetermined target";
        debug.println(" - ", pollMode);
        return;
    }

    switch (RfIntf.ModeTech) {  // Indetify card technology
      case (MODE_POLL | TECH_PASSIVE_NFCA):
        debug.print("\tSENS_RES = ");
        sprintf(tmp, "0x%.2X", RfIntf.Info.NFC_APP.SensRes[0]);
        debug.print(tmp);
        sensRes = tmp;
        debug.print(" ");
        sprintf(tmp, "0x%.2X", RfIntf.Info.NFC_APP.SensRes[1]);
        debug.print(tmp);
        sensRes += " " + String(tmp);
        debug.println(" ");

        debug.print("\tNFCID = ");
        nfcID = getHexRepresentation(RfIntf.Info.NFC_APP.NfcId, RfIntf.Info.NFC_APP.NfcIdLen);
        debug.println(nfcID);

        uidcf[2] = 7 + RfInterface.Info.NFC_APP.NfcIdLen;
        uidcf[3] = 0x02;
        uidcf[8] = 0x33;
        uidcf[9] = RfInterface.Info.NFC_APP.NfcIdLen;

        uidlen = RfInterface.Info.NFC_APP.NfcIdLen;

        memcpy(&uidcf[10], RfInterface.Info.NFC_APP.NfcId, RfInterface.Info.NFC_APP.NfcIdLen);

        debug.print("\tUIDCF: ");
        debug.println(getHexRepresentation(uidcf, uidlen + 10));

        // uidcf ready to fill CORE_CONF
        if (RfInterface.Info.NFC_APP.NfcIdLen != 4) {
          debug.println("Ooops... this doesn't seem to be a Mifare Classic card!");
        }

        if (RfIntf.Info.NFC_APP.SelResLen != 0) {
          debug.print("\tSEL_RES = ");
          sprintf(tmp, "0x%.2X", RfIntf.Info.NFC_APP.SelRes[0]);
          debug.print(tmp);
          selRes = tmp;
          debug.println(" ");
        }
        break;

      // Not tested
      case (MODE_POLL | TECH_PASSIVE_NFCB):
        if (RfIntf.Info.NFC_BPP.SensResLen != 0) {
          debug.print("\tSENS_RES = ");
          sensRes = getHexRepresentation(RfIntf.Info.NFC_BPP.SensRes, RfIntf.Info.NFC_BPP.SensResLen);
          debug.println(sensRes);
        }
        break;

      // Not tested
      case (MODE_POLL | TECH_PASSIVE_NFCF):
        debug.print("\tBitrate = ");
        debug.println((RfIntf.Info.NFC_FPP.BitRate == 1) ? "212" : "424");
        bitRate = (RfIntf.Info.NFC_FPP.BitRate == 1) ? "212" : "424";

        if (RfIntf.Info.NFC_FPP.SensResLen != 0) {
          debug.print("\tSENS_RES = ");
          sensRes = getHexRepresentation(RfIntf.Info.NFC_FPP.SensRes, RfIntf.Info.NFC_FPP.SensResLen);
          debug.println(sensRes);
        }
        break;

      // Not tested
      case (MODE_POLL | TECH_PASSIVE_15693):
        nfcID = getHexRepresentation(RfIntf.Info.NFC_VPP.ID, sizeof(RfIntf.Info.NFC_VPP.ID));
        debug.print("\tID = ");
        debug.println(nfcID);

        afi = String(RfIntf.Info.NFC_VPP.AFI);
        debug.print("\nAFI = ");
        debug.println(afi);

        dsfid = String(RfIntf.Info.NFC_VPP.DSFID, HEX);
        debug.print("\tDSFID = ");
        debug.println(dsfid);
        break;

      default:
        break;
    }
    if (RfIntf.MoreTags) {  // It will try to identify more NFC cards if they are the same technology
      if (nfc.ReaderActivateNext(&RfIntf) == NFC_ERROR) break;
    } else
      break;
  }
}

void setupNFC() {
  debug.println("Initializing NFC...");
  if (nfc.connectNCI()) {  // Wake up the board
    debug.println("Error while setting up the mode, check connections!");
    while (1)
      ;
  }

  if (nfc.ConfigureSettings()) {
    debug.println("The Configure Settings is failed!");
    while (1)
      ;
  }

  if (nfc.ConfigMode(mode)) {  // Set up the configuration mode
    debug.println("The Configure Mode is failed!!");
    while (1)
      ;
  }
}

void detectTags() {
  if (!nfc.WaitForDiscoveryNotification(&RfInterface, 1000)) {  // Waiting to detect cards
    displayCardInfo(RfInterface);
    switch (RfInterface.Protocol) {
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
    if (RfInterface.MoreTags) {
      nfc.ReaderActivateNext(&RfInterface);
    }
    //* Wait for card removal
    nfc.ProcessReaderMode(RfInterface, PRESENCE_CHECK);
    debug.println("CARD REMOVED!");
    nfcDiscoverySuccess = true;

    nfc.StopDiscovery();
    nfc.StartDiscovery(mode);
  }
  resetMode();
}
