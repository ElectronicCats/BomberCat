void printData(uint8_t* buff, uint8_t lenbuffer, uint8_t cmd) {
  char tmp[1];
  if (cmd == 1)
    Serial.print("\nCommand: ");
  else if (cmd == 2)
    Serial.print("\nReader command: ");
  else if (cmd == 3)
    Serial.print("\nBomberCat answer: ");
  else
    Serial.print("\nCard answer: ");

  for (uint8_t u = 0; u < lenbuffer; u++) {
    sprintf(tmp, "0x%.2X", buff[u]);
    Serial.print(tmp);
    Serial.print(" ");
  }
}

void emulateNFCID() {
  if (nfc.CardModeReceive(Cmd, &CmdSize) == 0) {  // Receive command from reader
    printData(Cmd, CmdSize, 1);
    printData(data, sizeof(data), 3);
    Serial.println("\nAttempts = " + String(attempts));
    attempts++;

    nfc.CardModeSend(data, sizeof(data));  // Emulate the dummy data and the NFCID

    // If Cmd is equal to requestCmd, then the reader is asking for the NFCID
    if (memcmp(Cmd, requestCmd, sizeof(requestCmd)) == 0) {
      Serial.println("Reader is asking for the NFCID");
      Serial.println("NFCID: " + getHexRepresentation(uidcf, sizeof(uidcf)));
    }
  }
}
