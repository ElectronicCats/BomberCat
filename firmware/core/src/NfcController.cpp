#include "NfcController.h"

#include "Log.h"

NfcController::NfcController(uint8_t irqPin, uint8_t venPin, uint8_t i2cAddress,
                            ChipModel chipModel)
    : _nfc(irqPin, venPin, i2cAddress, chipModel) {}

bool NfcController::reset() {
  // The PN7150 library uses 0 == success for these calls; the relay sketches
  // test them as `if (call()) { error }`, which we replicate exactly here so
  // the polarity matches the known-good firmware regardless of return type.
  if (_nfc.connectNCI()) {
    LOG_ERROR("NFC: connectNCI failed, check connections");
    return false;
  }
  if (_nfc.configureSettings()) {
    LOG_ERROR("NFC: configureSettings failed");
    return false;
  }
  if (_nfc.configMode()) {
    LOG_ERROR("NFC: configMode failed");
    return false;
  }
  _nfc.startDiscovery();
  return true;
}

// Order matters: connectNCI() (inside reset()) is what pulses VEN and calls
// Wire.begin() to wake the PN7150. The library's setReaderWriterMode() /
// setEmulationMode() are NOT mere flags — each runs a full mode+reset sequence
// that talks I2C (stopDiscovery/configMode/startDiscovery). Calling them before
// connectNCI() drives the I2C bus while Wire is still un-begun and the chip is
// asleep, which hangs the bus and wedges loop(). So bring the chip up first,
// then select the RF role — mirroring the known-good host_/client_Relay_NFC
// sketches (connectNCI at boot, setReaderWriterMode/setEmulationMode after).
bool NfcController::beginReaderMode() {
  if (!reset()) {
    return false;
  }
  return _nfc.setReaderWriterMode();
}

bool NfcController::beginEmulationMode() {
  if (!reset()) {
    return false;
  }
  if (!_nfc.setEmulationMode()) {
    return false;
  }
  // Re-arm discovery now that the mode is EMULATION. This mirrors the known-good
  // legacy sequence `nfc.setEmulationMode(); resetMode();`
  // (client_Relay_NFC.ino:1397), whose trailing resetMode() re-runs
  // connectNCI + configureSettings + configMode + startDiscovery *after* the
  // mode switch. Without it the chip is left armed only by setEmulationMode()'s
  // internal library reset(), which begins with stopDiscovery() and skips
  // configureSettings() when a protocol is already latched — leaving the RF
  // front-end not cleanly in listen/CARDEMU mode, so no terminal can activate
  // the emulated card (0 APDUs relayed; see DEBUG_card_emulation_no_rf_activation.md).
  if (!reset()) {
    return false;
  }
  return true;
}

bool NfcController::waitForTag(uint16_t timeoutMs) {
  return _nfc.isTagDetected(timeoutMs);
}

bool NfcController::readerTransceive(uint8_t *cmd, uint8_t cmdLen,
                                     uint8_t *resp, uint8_t *respLen,
                                     uint16_t timeoutMs) {
  _nfc.cardModeSend(cmd, cmdLen);

  // cardModeReceive() returns non-zero (NFC_ERROR) while no frame is ready and
  // 0 (NFC_SUCCESS) once one has been read. Spin until success or timeout.
  unsigned long start = millis();
  while (_nfc.cardModeReceive(resp, respLen)) {
    if (millis() - start > timeoutMs) return false;
  }
  return true;
}

bool NfcController::cardReceive(uint8_t *buf, uint8_t *len) {
  // Returns NFC_SUCCESS (0/false) when a frame was received; normalise to true.
  return !_nfc.cardModeReceive(buf, len);
}

bool NfcController::cardSend(uint8_t *buf, uint8_t len) {
  _nfc.cardModeSend(buf, len);
  return true;
}

bool NfcController::cardReArm() {
  // Library reset() = stopDiscovery + (configureSettings only if no protocol is
  // latched) + configMode + startDiscovery, in the current (EMULATION) mode.
  // This is exactly the re-arm the library runs on RF_DEACTIVATE_NTF inside
  // ProcessCardMode, minus the connectNCI() chip re-init that our heavier
  // reset() would do — so it recovers listen mode without dropping the chip.
  return _nfc.reset();
}
