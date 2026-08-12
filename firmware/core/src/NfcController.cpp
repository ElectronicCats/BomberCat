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

bool NfcController::beginReaderMode() {
  _nfc.setReaderWriterMode();
  return reset();
}

bool NfcController::beginEmulationMode() {
  _nfc.setEmulationMode();
  return reset();
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
