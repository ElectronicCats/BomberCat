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
  // A terminal left the field. The raw cardModeReceive() path never processes
  // the RF_DEACTIVATE_NTF (61 06) the PN7150 emits on field-off — unlike the
  // library's own ProcessCardMode(), which StopDiscovery+StartDiscovery on it —
  // so the emulated target is torn down and never rebuilt by the receive path.
  //
  // The light library reset() proved INSUFFICIENT to restore a detectable
  // ISO-DEP listen target after the first activation (the "worked exactly once,
  // dormant afterwards" symptom): it skips configureSettings() once a protocol
  // is latched and never re-pulses the chip, so the re-armed discovery does not
  // present the same target the fresh bring-up did. Re-run the FULL emulation
  // bring-up that provably presents a detectable card at boot (connectNCI +
  // configureSettings + configMode(EMU) + startDiscovery, then setEmulationMode
  // + a final reset — mode is already EMULATION here, so no RW detour). It costs
  // a chip re-init (~tens of ms) but only runs once per terminal departure.
  return beginEmulationMode();
}
