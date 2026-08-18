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
  // Send `cmd` into the RF field and wait for the tag's response frame into
  // `resp` (`respLen` set to the length). Returns false if the first data packet
  // does not arrive within timeoutMs. Reproduces the legacy host_Relay_NFC
  // double-receive (the answer is the SECOND data packet). timeoutMs bounds only
  // the wait for the first packet, and is a safety-net ceiling (a present card
  // answers fast); it must outlast an initial non-data getMessage() cycle
  // (~2 s each), so it is 4000 — the old 1000 killed the exchange at GPO.
  bool readerTransceive(uint8_t *cmd, uint8_t cmdLen, uint8_t *resp,
                        uint8_t *respLen, uint16_t timeoutMs = 4000);

  // How long readerTransceive waits (IRQ-polled) for a genuine SECOND response
  // packet before concluding the card is single-packet and returning the first
  // packet's answer. Two-packet cards deliver it in well under this (§17 reader
  // legs were ~450 ms total); single-packet cards would otherwise busy-wait the
  // library's hardcoded getMessage(2000). Tune here if a two-packet card ever
  // needs longer — but keep it far below the 2000 ms it replaces.
  //
  // LATENCY (Fase D, 2026-08-18): a SINGLE-packet card (the audit target) never
  // produces a second packet, so it busy-waits this FULL window on EVERY relayed
  // APDU — ~18 APDUs x this value of pure dead time per transaction. The window
  // only needs to outlast the gap between reading packet 1 and packet 2's IRQ on
  // a TWO-packet card, where packet 2 is ALREADY buffered in the PN7150 after the
  // single RF transceive (the chip just has to surface it and raise IRQ — a few
  // ms, not 120). Dropped 120 -> 25 ms to reclaim ~1.7 s on single-packet cards
  // while keeping ample margin for two-packet cards (they exit on IRQ, not on
  // this timeout). If a two-packet card ever mis-relays an intermediate frame,
  // raise this and record the working value in LATENCIA_OPTIMIZACION.md §Fase D.
  static const unsigned long SECOND_PACKET_WINDOW_MS = 25;

  // --- Card / HCE role ---------------------------------------------------
  // Non-blocking: pull one command frame from the terminal into `buf`
  // (`len` set to the length). Returns true when a frame was available.
  bool cardReceive(uint8_t *buf, uint8_t *len);

  // Push a response frame back to the terminal. Always returns true (the PN7150
  // library does not report a meaningful send status, so like its cardModeSend()
  // this reflects "sent", not "acknowledged"). Responses larger than one NCI data
  // packet (>255 B, e.g. an EMV READ RECORD certificate record) are fragmented
  // across NCI packets via the Packet Boundary Flag; the PN7150 reassembles them
  // into a single RF frame. (The library's own cardModeSend() emits only one
  // packet and caps at 255 B, so this replaces it rather than wrapping it.)
  bool cardSend(uint8_t *buf, uint16_t len);

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
  // Receive one NCI data packet WITHOUT the library cardModeReceive()'s useless
  // writeData(Ans,255) (~23 ms of garbage I2C per call; H2 / Fase C). Busy-polls
  // the public readData() (IRQ-gated) up to toutMs. Returns true and fills
  // pData/pDataSize on a data packet (header 0x00 0x00); false on timeout or a
  // non-data frame, leaving pData/pDataSize untouched. See NfcController.cpp.
  bool receiveNoGarbage(uint8_t *pData, uint8_t *pDataSize, uint16_t toutMs);

  Electroniccats_PN7150 _nfc;
};

#endif  // BOMBERCAT_CORE_NFCCONTROLLER_H
