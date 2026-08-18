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

  // --- APDU capture tap (Fase 8: pcap / Wireshark) -------------------------
  // When a sink is set, a COPY of every relayed APDU is emitted to it as one
  // structured event line:  ":apdu <dir> <ts_ms> <hex>\n"  where dir is "cmd"
  // (terminal -> card, i.e. PCD -> PICC) or "resp" (card -> terminal, i.e.
  // PICC -> PCD) and ts_ms is the device's millis() at that moment (ground-truth
  // timestamp). This runs OFF the hot path: the relay APDU itself still travels
  // over WiFi/TCP; this is only a copy for the host-side pcap writer
  // (tools/ `bombercat capture`). Pass nullptr to disable. The sink is normally
  // the same Serial the control REPL uses; the ":" marker keeps it distinct from
  // human log lines, matching SerialControl's leading-marker protocol.
  void setCapture(Print *sink) { _captureOut = sink; }
  bool capturing() const { return _captureOut != nullptr; }

 private:
  void handleFrame(const ServerData &sd, const NfcData &nfc);
  void readerHandleCommand(const NfcData &nfc);
  void cardPollTerminal();
  void cardHandleResponse(const NfcData &nfc);
  void emitCapture(const char *dir, const uint8_t *data, size_t len);

  NfcController &_nfc;
  NfcGateLink &_link;
  const RelayConfig &_cfg;

  State _state = State::Idle;
  bool _peerReady = false;
  bool _tagReady = false;  // READER: a physical card is currently activated
  bool _awaitingResponse = false;  // CARD: a command was forwarded, response due
  uint8_t _awaitTimeouts = 0;      // CARD: consecutive forwarded-but-unanswered
                                   // commands (dead-link detector; see below)
  uint32_t _relayed = 0;

  // Fase 8 APDU capture tap: when non-null, each relayed APDU is copied out as
  // a ":apdu <dir> <ts_ms> <hex>" event (see setCapture()). null = disabled.
  Print *_captureOut = nullptr;

  // Diagnostics / robustness (Fase 7 RF bring-up).
  unsigned long _lastHeartbeat = 0;  // throttles the per-role liveness log
  unsigned long _awaitStart = 0;     // CARD: millis() when _awaitingResponse latched

  // CARD emulation re-arm: the raw cardReceive path does not restart discovery
  // on RF_DEACTIVATE_NTF, so after a terminal leaves the emulated card goes
  // dormant. We re-arm once the card has been idle for REARM_IDLE_MS *after*
  // having seen terminal activity — recovering listen mode for the next
  // terminal without churning discovery when no terminal is ever present.
  unsigned long _lastCardActivity = 0;      // millis() of last frame from terminal
  bool _cardActivitySinceReArm = false;     // a terminal has activated us since re-arm

  // CARD: if the peer never returns a response (reader has no card, transceive
  // failed, or the relayed frame never reached it), don't deadlock forever —
  // clear _awaitingResponse after this long and re-poll the terminal.
  //
  // Budget note: on the first command of a new transaction the reader peer may
  // need to re-arm its RF front-end and re-activate the physical card (a stale
  // ISO-DEP session from the previous transaction) — a failed transceive can
  // block ~2s and a re-arm + re-activation + retry adds more. Keep this window
  // wide enough that a healthy-but-busy reader is not mistaken for a dead link
  // and torn down mid-recovery. See readerHandleCommand()'s retry loop.
  static const unsigned long AWAIT_TIMEOUT_MS = 5000;
  static const unsigned long HEARTBEAT_MS = 3000;

  // CARD emulation re-arm idle threshold. cardReArm() does a FULL chip re-init
  // (beginEmulationMode), which tears down the emulated card's ISO-DEP session —
  // fine BETWEEN transactions (recovering listen mode for the next terminal),
  // catastrophic DURING one. At 2000 ms this fired on a mere mid-transaction
  // pause: a terminal that took a beat to build the GPO after the SELECT AID FCI
  // came back to a re-armed (fresh) card and aborted, so the card never even
  // captured the GPO (intermittent: it only crossed when the terminal was fast
  // enough to beat the timer). A coupled EMV transaction completes in a few
  // seconds of continuous activity, so only a much longer gap reliably means the
  // terminal truly left — raised to 8000 so a normal inter-command pause can
  // never trip the re-arm mid-flow.
  static const unsigned long REARM_IDLE_MS = 8000;

  // CARD: how many consecutive forwarded-but-unanswered commands to tolerate
  // before we conclude the TCP link is dead and force a reconnect. WiFiNINA's
  // connected()/write() keep reporting success on a half-open socket (server
  // closed its side during an idle gap), so a forwarded frame silently never
  // reaches the server — its log shows no OP_PSH and the response can never
  // come. Forcing a reconnect is EXPENSIVE and destructive: it sends FIN, drops
  // the TCP for 3s (NFCGate.ino auto-retry) and can discard a response that was
  // already in flight — long enough to abort the terminal's EMV transaction.
  //
  // With a threshold of 1 the very FIRST 5s stall tore down the link, which is
  // exactly what broke Camino A (two BomberCats): on the first command of a
  // transaction the reader peer re-arms its RF front-end
  // (waitForTag + beginReaderMode + waitForTag + readerTransceive ≈ 2.5–4s, plus
  // WiFi/TCP round trips), so a legitimately-slow-but-healthy reader can brush
  // past AWAIT_TIMEOUT_MS on that first APDU. A threshold of 1 then reconnected
  // right as the reader was re-arming, losing the response and livelocking into
  // "relays zero". Against the NFCGate phone (Camino B2) the reader answers fast
  // with no re-arm, so this is inert there. Tolerate a few re-poll cycles first
  // (the terminal re-drives each one); only a genuinely dead half-open socket
  // stays silent across all of them and still reconnects (just later).
  static const uint8_t AWAIT_TIMEOUTS_BEFORE_RECONNECT = 3;

  // Max command/received-frame length handled in one go. NfcController's receive
  // primitives (cardModeReceive) report length in a single NCI byte, so an
  // inbound frame is capped at 255 B here; longer ones are dropped with a
  // warning. EMV commands fit comfortably.
  static const size_t RELAY_MAX_APDU = 255;

  // Max response length injected back to the terminal. Unlike commands, ISO-DEP
  // *responses* routinely exceed 255 B (EMV READ RECORD certificate records
  // reach 256 B — a 256 B response was silently dropped end-to-end in Camino B2,
  // see DEBUG_nfcgate_app_camino_b.md). NfcController::cardSend() fragments the
  // outbound NCI data packets (PBF), so the ceiling here is the NFCData.data
  // field capacity (512), not the 255 B single-packet limit.
  static const size_t RELAY_MAX_RESP = 512;
};

#endif  // BOMBERCAT_CORE_RELAYENGINE_H
