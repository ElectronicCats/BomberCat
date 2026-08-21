#include "NfcController.h"

#include "Log.h"

NfcController::NfcController(uint8_t irqPin, uint8_t venPin, uint8_t i2cAddress,
                            ChipModel chipModel)
    : _nfc(irqPin, venPin, i2cAddress, chipModel) {}

bool NfcController::receiveNoGarbage(uint8_t *pData, uint8_t *pDataSize,
                                     uint16_t toutMs) {
  // Faithful replica of the library's cardModeReceive() MINUS its useless
  // writeData(Ans, 255) (H2 / Fase C, LATENCIA_OPTIMIZACION.md §2, §4).
  //
  // That write transmits ~23 ms of *uninitialised* garbage on the I2C bus on
  // EVERY relayed receive: mbed_rp2040's Wire txBuffer is 256 B (>= 255), so
  // _wire->write(Ans,255) accepts all 255 bytes and endTransmission() actually
  // fires. getMessage() never consumes it — readData() is purely IRQ-gated
  // (reads only when the PN7150 raises IRQ HIGH, i.e. when the CHIP has data for
  // us, which our write does not request). Dropping it reclaims ~23 ms per
  // receive (~0.8-1.2 s/txn across both boards) with no functional change.
  //
  // Parsing mirrors cardModeReceive: a data packet has header 0x00 0x00,
  // buf[2] = payload length, payload at buf[3..].
  //
  // CRITICAL — skip non-data frames (this is what the library's outer
  // `while (cardModeReceive(...))` loop actually did, and dropping it broke the
  // reader in a first attempt at Fase C). In reader mode, after cardModeSend()
  // the PN7150 emits a CORE_CONN_CREDITS_NTF (0x60 0x06) control frame *before*
  // the tag's DATA response; other NTFs (e.g. RF_DEACTIVATE_NTF 0x61 0x06) can
  // also interleave. A single read that returned on the first frame handed back
  // the credits NTF (0x60 != 0x00), so the transceive reported failure on every
  // command. Keep reading (IRQ-gated) and IGNORE any non-data frame until a real
  // data packet arrives or toutMs elapses. On timeout, pData/pDataSize are left
  // untouched so callers keep any earlier value as a safe fallback.
  delay(1);  // kept for timing fidelity with the known-good path; 1 ms << 23 ms
  uint8_t buf[MAX_NCI_FRAME_SIZE];
  const unsigned long start = millis();
  while (millis() - start < toutMs) {
    uint32_t n = _nfc.readData(buf);  // public primitive; reads only when IRQ HIGH
    if (!n) continue;                 // no frame yet — keep polling within toutMs
    if (buf[0] == 0x00 && buf[1] == 0x00) {  // DATA packet = the answer
      *pDataSize = buf[2];
      memcpy(pData, &buf[3], *pDataSize);
      return true;
    }
    // Non-data frame (credits/notification): drop it and keep waiting for DATA.
  }
  return false;
}

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
  // the emulated card (0 APDUs relayed).
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

  // Reproduce the known-good legacy reader exchange (host_Relay_NFC seekTrack2,
  // used for EVERY relayed APDU — SELECT, GPO, READ RECORD — not only PPSE):
  // after a command the PN7150 hands back the tag's real answer on the SECOND
  // data packet; the first is an intermediate frame. So spin for the first data
  // packet (bounded by timeoutMs so a missing/removed card can't hang loop()),
  // then read ONCE MORE — that second frame is the actual APDU response.
  //
  // receiveNoGarbage() returns true once a data packet is in `resp` and false
  // otherwise, and it leaves resp/respLen untouched on a non-data frame — so if
  // no distinct second packet arrives the first packet stays put as a safe
  // fallback. It replaces the library's cardModeReceive() to drop the ~23 ms
  // garbage I2C write (H2 / Fase C); it busy-polls readData() (IRQ-gated) up to
  // timeoutMs, so a missing/removed card still can't hang loop().
  //
  // The single-receive refactor that replaced this returned the first
  // (intermediate) frame, and with the previous 1000 ms cap it reported a false
  // transceive timeout the moment the first receive cycle came back without
  // data. That broke every transaction at GPO (the first non-SELECT command) and
  // then tripped the destructive mid-transaction re-arm in readerHandleCommand.
  if (!receiveNoGarbage(resp, respLen, timeoutMs)) return false;

  // The legacy path then read a SECOND packet unconditionally, because some
  // cards answer on the second data packet (the first being an intermediate
  // frame). The library's cardModeReceive() hardcoded an internal
  // getMessage(2000): when the card sends only ONE data packet (its answer is
  // already in `resp`), that second read finds nothing and busy-waits the FULL
  // 2000 ms on EVERY relayed APDU — ~2 s of dead time per command, enough to
  // make an EMV terminal abandon
  // the transaction (observed on hardware: reader legs ~2.4 s, txn dies at READ
  // RECORD). This directly contradicts docs/NFCGATE_PLAN.md §17's assumption that the
  // second receive never hits the 2000 ms ceiling — hardware shows it does.
  //
  // IRQ-gate it: the PN7150 drives its IRQ line HIGH only when a frame is
  // pending, so poll hasMessage() for a short window. If a genuine second packet
  // arrives (two-packet cards — fast, well under this window per §17's ~450 ms
  // legs) read it as the real answer; if the window elapses with no IRQ
  // (single-packet cards) keep the first packet's answer and return at once.
  // Preserves both card behaviors; removes the per-APDU dead wait.
  const unsigned long secondStart = millis();
  while (!_nfc.hasMessage()) {
    if (millis() - secondStart >= SECOND_PACKET_WINDOW_MS) {
      return true;  // single-packet card: the answer is already in `resp`
    }
  }
  // IRQ is HIGH: a frame is waiting, so this read returns at once.
  receiveNoGarbage(resp, respLen, 50);  // genuine second data packet = the answer
  return true;
}

bool NfcController::cardReceive(uint8_t *buf, uint8_t *len) {
  // Non-blocking-ish pull of one terminal command. Uses receiveNoGarbage (H2 /
  // Fase C) to skip the library's ~23 ms garbage I2C write; keeps the library's
  // 2000 ms IRQ-gated wait so an idle poll blocks the same as before (returns
  // immediately when the terminal actually sends). Returns true on a data frame.
  return receiveNoGarbage(buf, len, 2000);
}

bool NfcController::cardSend(uint8_t *buf, uint16_t len) {
  // A single NCI data packet carries its payload length in one byte, so it caps
  // at 255 B — but ISO-DEP responses can be larger (EMV records reach 256 B).
  // The library's cardModeSend() only ever emits one packet, silently capping
  // the relay at 255 B (a 256 B READ RECORD response was dropped end-to-end).
  // Fragment here instead: emit <=255 B NCI data
  // packets and set the Packet Boundary Flag (PBF, bit 4 of the header) on every
  // segment except the last, so the PN7150 reassembles them into one RF frame.
  // Header layout matches the library's cardModeSend(): [conn-id/PBF][RFU=0][len].
  //
  // writeData()'s return is deliberately IGNORED, exactly as the library's
  // cardModeSend() does ((void)writeData). It surfaces I2C endTransmission()
  // codes that are transiently non-zero on a healthy bus (the PN7150 NACKs the
  // odd frame); EMV terminals simply re-issue the command. Treating it as fatal
  // here (aborting the send + logging) turned that harmless hiccup into a visible
  // failure and a Camino A regression — so for a <=255 B response this is now
  // byte-for-byte the same I2C sequence as the known-good cardModeSend().
  const uint16_t MAX_SEG = 255;
  uint8_t pkt[3 + MAX_SEG];
  uint16_t off = 0;
  do {
    uint16_t seg = len - off;
    bool last = true;
    if (seg > MAX_SEG) {
      seg = MAX_SEG;
      last = false;  // more segments follow -> set PBF on this one
    }
    pkt[0] = last ? 0x00 : 0x10;  // conn id 0; bit 4 = PBF
    pkt[1] = 0x00;
    pkt[2] = (uint8_t)seg;
    memcpy(&pkt[3], buf + off, seg);
    (void)_nfc.writeData(pkt, (uint32_t)seg + 3);
    off += seg;
  } while (off < len);
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
