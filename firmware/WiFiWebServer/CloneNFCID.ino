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
    Serial.print(tmp);
    Serial.print(" ");
  }
}

void emulateNFCID() {
  mode = 2;
  resetMode();
  memcpy(&card[0], last, sizeof(last));
  memcpy(&card[4], token, sizeof(token));
  memcpy(&card[23], statusapdu, sizeof(statusapdu));

  uint8_t* apdus2[] = {ppsea, visaa, processinga, card, finished, finished};
  uint8_t apdusLen2[] = {sizeof(ppsea), sizeof(visaa), sizeof(processinga), sizeof(card), sizeof(finished), sizeof(finished)};

  unsigned long lastTime = millis();
  uint8_t counter = 0;

  // while(true) {
  //   nfc.CardModeSend(apdus2[0], apdusLen2[0]);
  //   printData(apdus2[0], apdusLen2[0], 3);
  // }

  for (uint8_t i = 0; i < 6; i++) {
    // Serial.println("i = " + String(i));

    if (nfc.CardModeReceive(Cmd, &CmdSize) == 0) {  // Data in buffer?

      while ((CmdSize < 2) && (Cmd[0] != 0x00)) {
      }

      printData(Cmd, CmdSize, 1);

      nfc.CardModeSend(apdus2[i], apdusLen2[i]);

      printData(apdus2[i], apdusLen2[i], 3);
      Serial.println("counter = " + String(++counter));
      // counter++;

      // if (++counter == 6) break;

    } else {
      i--;
    }
  }
}