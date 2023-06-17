void emulateNFCID() {
  mode = 2;
  resetMode();
  memcpy(&card[0], last, sizeof(last));
  memcpy(&card[4], token, sizeof(token));
  memcpy(&card[23], statusapdu, sizeof(statusapdu));

  uint8_t* apdus2[] = {ppsea, visaa, processinga, card, finished, finished};
  uint8_t apdusLen2[] = {sizeof(ppsea), sizeof(visaa), sizeof(processinga), sizeof(card), sizeof(finished), sizeof(finished)};

  unsigned long lastTime = millis();
  int counter = 0;

  for (uint8_t i = 0; i < 6; i++) {
    Serial.println("Waiting for APDU");
    Serial.println("i = " + String(i));

    if (nfc.CardModeReceive(Cmd, &CmdSize) == 0) {  // Data in buffer?

      while ((CmdSize < 2) && (Cmd[0] != 0x00)) {
      }

      printData(Cmd, CmdSize, 1);

      nfc.CardModeSend(apdus2[i], apdusLen2[i]);

      printData(apdus2[i], apdusLen2[i], 3);

    } else {
      i--;
    }
    // if (millis() - lastTime > 10000) {
    //   Serial.println("Timeout");
    //   break;
    // }
  }
}