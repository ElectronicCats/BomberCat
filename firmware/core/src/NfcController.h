/**
 * BomberCatCore - NfcController
 *
 * Thin wrapper over Electroniccats_PN7150 that absorbs the resetMode() flow and
 * the cardModeSend/cardModeReceive exchange sequences duplicated across
 * host_Relay_NFC and client_Relay_NFC.
 *
 * Two design choices vs. the legacy sketches:
 *   1. Setup failures RETURN false instead of hanging in `while (1);`, so the
 *      relay engine / CLI can report the error over serial.
 *   2. A normalised "true = success" convention hides the PN7150 library's
 *      mixed 0-is-success / bool return values.
 *
 * The RF primitives are wrapped as-is; use raw() to reach anything not exposed
 * here (e.g. remoteDevice metadata) or to reproduce a legacy call sequence
 * verbatim during migration.
 *
 * Distributed as-is; no warranty is given.
 */
#ifndef BOMBERCAT_CORE_NFCCONTROLLER_H
#define BOMBERCAT_CORE_NFCCONTROLLER_H

#include <Arduino.h>

#include "Electroniccats_PN7150.h"

// BomberCat PN7150 wiring (see host/client sketches).
static const uint8_t BOMBERCAT_PN7150_IRQ = 11;
static const uint8_t BOMBERCAT_PN7150_VEN = 13;
static const uint8_t BOMBERCAT_PN7150_ADDR = 0x28;

class NfcController {
 public:
  NfcController(uint8_t irqPin = BOMBERCAT_PN7150_IRQ,
                uint8_t venPin = BOMBERCAT_PN7150_VEN,
                uint8_t i2cAddress = BOMBERCAT_PN7150_ADDR,
                ChipModel chipModel = PN7150);

  // Re-run the NCI bring-up: connectNCI + configureSettings + configMode +
  // startDiscovery, in the current mode. Mirrors the sketches' resetMode() but
  // returns false on any step failing instead of hanging. Call after selecting
  // a mode, or to recover discovery between transactions.
  bool reset();

  // Switch to reader/writer mode (reads a physical card) and reset(). READER
  // role in NFCGate terms.
  bool beginReaderMode();

  // Switch to card emulation / HCE mode (presents as a card to a terminal) and
  // reset(). CARD role in NFCGate terms.
  bool beginEmulationMode();

  // Block up to timeoutMs waiting for a tag/terminal to enter the field.
  // Returns true when one is detected.
  bool waitForTag(uint16_t timeoutMs = 500);

  // --- Reader role -------------------------------------------------------
  // Send `cmd` into the RF field and wait for a single response frame into
  // `resp` (`respLen` set to the length). Returns false if no response arrives
  // within timeoutMs. This is the clean equivalent of the sketches'
  // exchangeReader flow; it does not reproduce the legacy double-receive.
  bool readerTransceive(uint8_t *cmd, uint8_t cmdLen, uint8_t *resp,
                        uint8_t *respLen, uint16_t timeoutMs = 1000);

  // --- Card / HCE role ---------------------------------------------------
  // Non-blocking: pull one command frame from the terminal into `buf`
  // (`len` set to the length). Returns true when a frame was available.
  bool cardReceive(uint8_t *buf, uint8_t *len);

  // Push a response frame back to the terminal. Returns true once queued.
  // (The PN7150 library does not report send status, so this reflects "sent",
  // not "acknowledged".)
  bool cardSend(uint8_t *buf, uint8_t len);

  // Re-arm card-emulation discovery after a terminal has left the RF field.
  // The raw cardReceive path swallows the PN7150's RF_DEACTIVATE_NTF without
  // restarting discovery (unlike the library's ProcessCardMode), so once a
  // terminal deactivates, the chip stops listening and no further terminal can
  // activate the emulated card. This performs the same re-arm ProcessCardMode
  // does — stopDiscovery + configMode + startDiscovery in the already-selected
  // EMULATION mode, WITHOUT a full connectNCI chip re-init. Returns true on
  // success.
  bool cardReArm();

  // Escape hatch for advanced use / verbatim legacy sequences.
  Electroniccats_PN7150 &raw() { return _nfc; }

 private:
  Electroniccats_PN7150 _nfc;
};

#endif  // BOMBERCAT_CORE_NFCCONTROLLER_H
