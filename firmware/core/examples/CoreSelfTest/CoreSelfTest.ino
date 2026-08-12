/**
 * BomberCatCore - CoreSelfTest
 *
 * Compile/smoke sketch for the Fase 2 core skeleton. It does not perform a real
 * relay; it just exercises every public symbol so the library builds cleanly on
 * the target (Arduino Mbed OS RP2040 core) and demonstrates basic usage.
 *
 * Board: Arduino Mbed OS RP2040 (BomberCat / RP2040).
 *
 * Distributed as-is; no warranty is given.
 */
#include <BomberCatCore.h>

NfcController nfc;
ConfigStore config;
RelayConfig cfg;

void setup() {
  Serial.begin(9600);
  Log::begin(Serial, LogLevel::Debug);
  LOG_INFO("BomberCatCore self-test");

  // --- ConfigStore ---
  if (config.begin()) {
    if (!config.load(cfg)) {
      LOG_WARN("No saved config, using defaults");
      cfg = ConfigStore::defaults();
    }
    Log::out().print("session=");
    Log::out().print(cfg.session);
    Log::out().print(" role=");
    Log::out().println(cfg.roleEnum() == RelayRole::READER ? "reader" : "card");
  } else {
    LOG_ERROR("ConfigStore init failed");
  }

  // --- HexUtils / Log ---
  uint8_t ppse[] = {0x00, 0xA4, 0x04, 0x00, 0x0E};
  Log::hex(LogLevel::Debug, "SELECT PPSE:", ppse, sizeof(ppse));
  Serial.println(HexUtils::toString(ppse, sizeof(ppse)));

  // --- NfcController ---
  bool ok = (cfg.roleEnum() == RelayRole::READER) ? nfc.beginReaderMode()
                                                   : nfc.beginEmulationMode();
  if (!ok) {
    LOG_ERROR("NFC bring-up failed");
    return;
  }
  LOG_INFO("NFC ready");
}

void loop() {
  uint8_t buf[256];
  uint8_t len = 0;

  if (cfg.roleEnum() == RelayRole::READER) {
    // Reader: (a peer would supply the terminal command; here we just probe.)
    if (nfc.waitForTag(500)) {
      LOG_DEBUG("Tag in field");
    }
  } else {
    // Card/HCE: forward any command frame from the terminal.
    if (nfc.cardReceive(buf, &len)) {
      Log::hex(LogLevel::Debug, "C->:", buf, len);
    }
  }
  delay(50);
}
