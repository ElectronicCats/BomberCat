/**
 * BomberCatCore - RelayEngine
 *
 * Ties NfcController (RF) and NfcGateLink (nfcgate-server transport) into a
 * cooperative, non-blocking relay. It owns the session handshake (OP_SYN on
 * connect, OP_ACK in reply to a peer's SYN — matching the NFCGate app's
 * NetworkManager.java) and the per-role APDU shuttling.
 *
 * WiFi association is deliberately NOT done here: the sketch calls WiFi.begin()
 * and hands a connected Client (WiFiClient) to NfcGateLink, keeping this class
 * — and all of core/ — free of any WiFiNINA dependency (same decoupling as
 * NfcGateLink). This class only drives the relay once the radio is up.
 *
 * Data-source semantics (proto/UPSTREAM.md, validated against the pinned
 * server): a frame tagged READER is a *command* (terminal -> card); a frame
 * tagged CARD is the *response* (card -> terminal). So:
 *   - READER role: consumes READER-tagged commands, replays them to the physical
 *     card, and produces CARD-tagged responses.
 *   - CARD/HCE role: reads the terminal's commands over RF and produces
 *     READER-tagged commands, then consumes the CARD-tagged responses that come
 *     back over TCP and injects them to the terminal.
 *
 * Both roles are implemented end to end (READER: Fase 4, CARD/HCE: Fase 5).
 *
 * Distributed as-is; no warranty is given.
 */
#ifndef BOMBERCAT_CORE_RELAYENGINE_H
#define BOMBERCAT_CORE_RELAYENGINE_H

#include <Arduino.h>

#include "ConfigStore.h"
#include "NfcController.h"
#include "NfcGateLink.h"

class RelayEngine {
 public:
  enum class State : uint8_t {
    Idle,       // before begin()
    Relaying,   // NFC up, link connected, SYN sent — shuttling APDUs
    Error,      // NFC bring-up, link, or handshake failed / link dropped
  };

  // `nfc`, `link` and `cfg` must outlive the engine (all are typically globals
  // in the sketch).
  RelayEngine(NfcController &nfc, NfcGateLink &link, const RelayConfig &cfg);

  // Bring up NFC in the configured role, open the TCP link to nfcgate-server,
  // and send OP_SYN (which both announces presence and registers this client
  // with the session). Assumes WiFi is already associated. Returns true on
  // success; on failure sets state() to Error and returns false.
  //
  // This is the convenience wrapper that runs the three bring-up steps below in
  // one (blocking) call. The NFCGate sketch drives them individually from its
  // non-blocking `run` state machine so the serial REPL stays responsive; see
  // beginNfc()/connectLink()/announce().
  bool begin();

  // --- Individual bring-up steps (for a non-blocking, phased `run`) ---------
  // Each returns true on success; on failure logs, sets state() to Error and
  // returns false. Call in order: beginNfc -> connectLink -> announce.

  // Step 1: bring up the PN7150 in the configured role's RF mode. Also resets
  // the per-session flags, so this is the start of a fresh bring-up.
  bool beginNfc();

  // Step 2: open the TCP link to nfcgate-server (bounded by the WiFiClient's
  // connection timeout set in the sketch). Assumes WiFi is associated.
  bool connectLink();

  // Step 3: send OP_SYN to announce presence + register with the session. On
  // success moves state() to Relaying.
  bool announce();

  // One cooperative, non-blocking step of the relay. Call once per loop().
  // Drains any pending server frames (READER: services one card command per
  // frame; CARD: injects a response to the terminal) and, in CARD role, polls
  // the terminal for a new command to forward.
  void loop();

  // Send OP_FIN and close the link.
  void stop();

  State state() const { return _state; }
  bool connected() const { return _link.connected(); }

  // True once the peer's presence has been observed (its SYN or ACK seen).
  bool peerReady() const { return _peerReady; }

  // Count of command/response APDU pairs relayed (READER role).
  uint32_t relayedCount() const { return _relayed; }

 private:
  void handleFrame(const ServerData &sd, const NfcData &nfc);
  void readerHandleCommand(const NfcData &nfc);
  void cardPollTerminal();
  void cardHandleResponse(const NfcData &nfc);

  NfcController &_nfc;
  NfcGateLink &_link;
  const RelayConfig &_cfg;

  State _state = State::Idle;
  bool _peerReady = false;
  bool _tagReady = false;  // READER: a physical card is currently activated
  bool _awaitingResponse = false;  // CARD: a command was forwarded, response due
  uint32_t _relayed = 0;

  // Max APDU length either role handles in one frame. NfcController's
  // reader/card primitives use uint8_t lengths (as the legacy sketches do), so
  // a single frame is capped at 255 B here; longer NFCData frames are dropped
  // with a warning. EMV short APDUs fit comfortably.
  static const size_t RELAY_MAX_APDU = 255;
};

#endif  // BOMBERCAT_CORE_RELAYENGINE_H
