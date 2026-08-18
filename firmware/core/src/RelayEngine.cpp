/**
 * BomberCatCore - RelayEngine implementation.
 *
 * Distributed as-is; no warranty is given.
 */
#include "RelayEngine.h"

#include "Log.h"

RelayEngine::RelayEngine(NfcController &nfc, NfcGateLink &link,
                         const RelayConfig &cfg)
    : _nfc(nfc), _link(link), _cfg(cfg) {}

bool RelayEngine::begin() {
  // Convenience wrapper: run the three steps back to back (blocking). The
  // sketch's non-blocking `run` calls the steps individually instead.
  return beginNfc() && connectLink() && announce();
}

bool RelayEngine::beginNfc() {
  _peerReady = false;
  _tagReady = false;
  _awaitingResponse = false;
  _awaitTimeouts = 0;
  _lastCardActivity = millis();
  _cardActivitySinceReArm = false;

  // Bring up the PN7150 in the role's RF mode.
  const bool isReader = _cfg.roleEnum() == RelayRole::READER;
  const bool nfcOk = isReader ? _nfc.beginReaderMode() : _nfc.beginEmulationMode();
  if (!nfcOk) {
    LOG_ERROR("RelayEngine: NFC bring-up failed");
    _state = State::Error;
    return false;
  }
  LOG_INFO(isReader ? "RelayEngine: NFC reader mode ready"
                    : "RelayEngine: NFC emulation mode ready");
  return true;
}

bool RelayEngine::connectLink() {
  // Open the TCP link and adopt the session byte.
  if (!_link.connect(_cfg.server, _cfg.port, _cfg.session)) {
    LOG_ERROR("RelayEngine: link connect failed");
    _state = State::Error;
    return false;
  }
  return true;
}

bool RelayEngine::announce() {
  // Announce + register with the session (OP_SYN, as the NFCGate app does). The
  // server only associates a client with a session once it sends a frame, so
  // this is what makes us able to receive the peer's commands.
  if (!_link.sendControl(NfcOpcode::SYN)) {
    LOG_ERROR("RelayEngine: SYN failed");
    _state = State::Error;
    return false;
  }

  _state = State::Relaying;
  LOG_INFO("RelayEngine: relaying");
  return true;
}

void RelayEngine::loop() {
  if (_state != State::Relaying) {
    return;
  }
  if (!_link.connected()) {
    LOG_WARN("RelayEngine: link lost");
    _state = State::Error;
    return;
  }

  // Diagnostic heartbeat for BOTH roles: the reader path is fully frame-driven
  // and otherwise prints nothing, so without this its monitor looks dead even
  // when the loop is healthy. Throttled to once every HEARTBEAT_MS.
  const unsigned long nowMs = millis();
  if (nowMs - _lastHeartbeat >= HEARTBEAT_MS) {
    _lastHeartbeat = nowMs;
    if (_cfg.roleEnum() == RelayRole::READER) {
      LOG_INFO(_peerReady ? "reader: vivo, peer presente, esperando comando del peer"
                          : "reader: vivo, sin peer aun");
    } else {
      LOG_INFO(_awaitingResponse ? "card: esperando respuesta del peer (relay)"
                                 : "card: esperando comando del terminal (RF)");
    }
  }

  // Drain every complete frame that has arrived. poll() is non-blocking and
  // returns one frame per call: 1 = frame, 0 = nothing yet, -1 = link reset.
  ServerData sd;
  NfcData nfc;
  int r;
  while ((r = _link.poll(sd, nfc)) == 1) {
    handleFrame(sd, nfc);
  }
  if (r < 0) {
    LOG_WARN("RelayEngine: link reset while polling");
    _state = State::Error;
    return;
  }

  // CARD/HCE role: pull a command from the terminal (RF) and forward it. The
  // reader role is fully frame-driven (handled above); only the card role needs
  // to actively poll the RF side.
  if (_cfg.roleEnum() == RelayRole::CARD) {
    cardPollTerminal();
  }
}

void RelayEngine::stop() {
  if (_link.connected()) {
    _link.sendControl(NfcOpcode::FIN);
    _link.stop();
  }
  _state = State::Idle;
}

void RelayEngine::handleFrame(const ServerData &sd, const NfcData &nfc) {
  // Log every frame the server relays to us: tells us at a glance whether the
  // reader ever actually receives the card's forwarded command (op=PSH,
  // src=READER) and whether the card receives the response (op=PSH, src=CARD).
  if (Log::enabled(LogLevel::Info)) {
    String m = "frame rx: op=";
    m += (int)sd.opcode;
    m += " src=";
    m += (int)nfc.data_source;
    m += " len=";
    m += (int)nfc.data.size;
    Log::line(LogLevel::Info, m);
  }

  switch ((NfcOpcode)sd.opcode) {
    case NfcOpcode::SYN:
      // Peer announced itself; acknowledge and mark it present.
      LOG_DEBUG("RelayEngine: peer SYN");
      _peerReady = true;
      _link.sendControl(NfcOpcode::ACK);
      break;

    case NfcOpcode::ACK:
      LOG_DEBUG("RelayEngine: peer ACK");
      _peerReady = true;
      break;

    case NfcOpcode::FIN:
      LOG_INFO("RelayEngine: peer FIN");
      _peerReady = false;
      break;

    case NfcOpcode::PSH:
      // The real NFCGate reader peer emits a one-off data_type=INITIAL frame
      // carrying the physical tag's config (anticollision/ATS bytes), NOT an
      // APDU. We can't apply that to the PN7150 (no ATS API) and treating it as
      // an APDU would inject garbage to the terminal (a bogus `C-> term resp:`)
      // or replay it to the card. Only CONTINUATION frames are real APDUs.
      if ((NfcType)nfc.data_type == NfcType::INITIAL) {
        LOG_DEBUG("RelayEngine: trama INITIAL del peer (config de tag) ignorada");
        break;
      }
      if (_cfg.roleEnum() == RelayRole::READER) {
        // Only a command (READER-tagged) is ours to service; a CARD-tagged PSH
        // is a response we ourselves produce, so ignore it here.
        if ((NfcSource)nfc.data_source == NfcSource::READER) {
          _peerReady = true;  // a command implies the peer is live
          readerHandleCommand(nfc);
        }
      } else {
        // CARD/HCE role: a CARD-tagged PSH is the physical card's response
        // (relayed by our reader peer); inject it back to the terminal. A
        // READER-tagged PSH is a command we produced ourselves, so ignore it.
        if ((NfcSource)nfc.data_source == NfcSource::CARD) {
          _peerReady = true;  // a response implies the peer is live
          cardHandleResponse(nfc);
        }
      }
      break;
  }
}

void RelayEngine::readerHandleCommand(const NfcData &nfc) {
  const size_t cmdLen = nfc.data.size;
  if (cmdLen == 0) {
    return;  // nothing to replay
  }
  if (cmdLen > RELAY_MAX_APDU) {
    LOG_WARN("RelayEngine: command APDU too long for reader path, dropping");
    return;
  }
  Log::hex(LogLevel::Debug, "R<- cmd:", nfc.data.bytes, cmdLen);
  emitCapture("cmd", nfc.data.bytes, cmdLen);

  // nfc.data.bytes is const here; NfcController's API takes a non-const buffer
  // (it never writes the command), so copy into a local scratch to transceive.
  uint8_t cmd[RELAY_MAX_APDU];
  memcpy(cmd, nfc.data.bytes, cmdLen);
  uint8_t resp[RELAY_MAX_APDU];
  uint8_t respLen = 0;

  // Service the command, re-activating the physical card as needed. We keep the
  // card activated across ONE transaction (_tagReady), but BETWEEN transactions
  // its ISO-DEP session dies during the idle gap and the raw reader path never
  // re-polls — so the first command of the next transaction finds a stale
  // _tagReady, the transceive times out, and the command would be dropped with
  // no response. The card peer then never gets an answer and reconnects forever
  // (DEBUG_card_stale_link_second_txn.md). This is the reader-side analog of the
  // card's re-arm: on a failed activation OR a failed exchange, re-arm the reader
  // front-end and retry once before giving up, so a new transaction self-heals.
  //
  // Re-sending a command after a re-arm is safe for the transaction-boundary
  // commands where this actually triggers (SELECT PPSE is idempotent); a
  // mid-transaction session death is rare (commands ~0.5s apart keep it alive).
  //
  // WTX-BUDGET HARDENING (2026-08-18): the self-heal above (full beginReaderMode
  // re-arm + a SECOND up-to-4 s transceive) is only safe and affordable at a
  // TRANSACTION BOUNDARY — the first command after the idle gap, where _tagReady
  // is stale-false and the command (SELECT PPSE) is idempotent. Doing it
  // MID-transaction is doubly wrong: (a) it burns a second ~4 s window on top of
  // the first, and the card peer must hold the terminal that whole time with
  // ISO-DEP S(WTX) requests — enough back-to-back extensions exhaust the
  // terminal's Waiting-Time-Extension budget and it aborts the transaction
  // blaming latency, not a decline; and (b) it would re-issue a possibly
  // NON-idempotent APDU (e.g. GENERATE AC) against a freshly re-armed ISO-DEP
  // session, which is protocol-invalid. So we gate the expensive path on whether
  // the session was already live for THIS transaction: sessionWasLive == false
  // means we are at the boundary (self-heal), true means mid-transaction
  // (fail fast — one transceive window, no re-arm, no replay — and let the card
  // peer's AWAIT_TIMEOUT_MS recovery take it from there). This keeps any single
  // command inside one transceive budget so the WTX ceiling is never approached.
  //
  // BOUNDARY PERSISTENCE (2026-08-18, complement of the above): the flip side of
  // fail-fast is that at the boundary we should try HARDER, not give up on the
  // first miss. A marginal card (present but poorly coupled) can miss a single
  // discovery window, and the old fixed "2 attempts" dropped the whole
  // transaction after ~1 s. So the boundary now loops discovery windows + re-arms
  // until READER_BOUNDARY_ACTIVATE_MS elapses (sized under AWAIT_TIMEOUT_MS), and
  // the same time budget also caps the boundary self-heal so it can never stack a
  // second ~4 s transceive past the WTX budget. Both regimes key off the SINGLE
  // gate `!sessionWasLive && within budget`: mid-transaction (sessionWasLive)
  // fail-fasts on the first miss; the boundary persists within its budget.
  const bool sessionWasLive = _tagReady;
  const unsigned long cmdStart = millis();
  for (;;) {
    // Boundary self-heal / persistence is allowed only until the budget runs out;
    // after that (and always mid-transaction) we drop the command instead.
    const bool mayRecover =
        !sessionWasLive && (millis() - cmdStart < READER_BOUNDARY_ACTIVATE_MS);

    if (!_tagReady) {
      if (_nfc.waitForTag(500)) {
        _tagReady = true;
        LOG_DEBUG("reader: tarjeta activada");
      } else if (mayRecover) {
        LOG_WARN("reader: sin tarjeta en campo; re-armando discovery y reintentando (borde)");
        _nfc.beginReaderMode();  // full re-arm (reset + reader mode), as at boot
        continue;                // retry activation while budget remains
      } else {
        LOG_WARN("reader: sin tarjeta; descartando comando");
        return;
      }
    }

    if (_nfc.readerTransceive(cmd, (uint8_t)cmdLen, resp, &respLen)) {
      Log::hex(LogLevel::Debug, "R-> resp:", resp, respLen);
      emitCapture("resp", resp, respLen);
      // CONTINUATION, not INITIAL: an APDU is never the peer's one-off tag
      // config. The real NFCGate app routes data_type=INITIAL into its daemon
      // config parser (NfcManager.applyData) instead of transceiving/injecting
      // it, which mis-handles or crashes the app. (Two BomberCats ignore
      // data_type, so this was invisible in Camino A.)
      if (!_link.send(NfcSource::CARD, resp, respLen, NfcType::CONTINUATION)) {
        LOG_ERROR("RelayEngine: failed to send response");
        return;
      }
      _relayed++;
      return;  // relayed one command/response pair
    }

    // Transceive failed/timed out. Drop readiness. Only re-arm + retry at a
    // TRANSACTION BOUNDARY with budget left: there the command is the idempotent
    // SELECT PPSE and the card session was already stale, so a full re-arm is the
    // intended self-heal. MID-transaction (sessionWasLive), or once the boundary
    // budget is spent, fail fast instead — a second ~4 s transceive window would
    // over-run the terminal's WTX budget, and replaying a non-idempotent APDU
    // against a re-armed session is invalid. Returning with no response hands
    // recovery to the card peer's AWAIT_TIMEOUT_MS path. Recompute the budget
    // here: the transceive above may have consumed part of it (up to ~4 s).
    _tagReady = false;
    if (!sessionWasLive && millis() - cmdStart < READER_BOUNDARY_ACTIVATE_MS) {
      LOG_WARN("reader: transceive fallo/timeout; re-activando (borde de transacción)");
      _nfc.beginReaderMode();
    } else {
      LOG_WARN("reader: transceive fallo/timeout; fail-fast (presupuesto WTX/tiempo)");
      return;
    }
  }
}

void RelayEngine::cardPollTerminal() {
  // Strict request/response: only ask the terminal for a new command once the
  // previous one's response has been injected. The terminal itself won't issue
  // the next command until it is answered, so this keeps us in lock-step.
  //
  // BUT never latch here forever: if the peer reader can't service the command
  // (it never received the relayed frame, has no physical card in field, or the
  // transceive failed — all of which leave readerHandleCommand producing no
  // response), a permanent _awaitingResponse deadlock would stop us polling the
  // terminal for good. Time it out and resume so the terminal can be re-read.
  if (_awaitingResponse) {
    if (millis() - _awaitStart < AWAIT_TIMEOUT_MS) {
      return;
    }
    LOG_WARN("card: timeout esperando respuesta del peer; re-poll del terminal");
    _awaitingResponse = false;

    // We forwarded a command and got nothing back in time. On WiFiNINA a
    // server-side close leaves connected() == true and write() reporting the
    // full byte count into a dead (half-open) socket, so the forwarded frame
    // silently never reached the server (its log shows no OP_PSH) and the
    // response can never come. After enough of these, stop re-polling the
    // terminal into a permanent loop and force a reconnect via the sketch's
    // auto-retry (State::Error -> stop() -> reconnect TCP + re-SYN).
    if (++_awaitTimeouts >= AWAIT_TIMEOUTS_BEFORE_RECONNECT) {
      LOG_WARN("card: enlace probablemente caido (respuesta perdida); forzando reconexion");
      _awaitTimeouts = 0;
      _state = State::Error;
      return;
    }
  }

  uint8_t cmd[RELAY_MAX_APDU];
  uint8_t cmdLen = 0;
  if (!_nfc.cardReceive(cmd, &cmdLen)) {
    // No command right now. If a terminal was here and has since left the field
    // (idle for REARM_IDLE_MS), re-arm the emulation discovery ONCE so the next
    // terminal can activate us again. Without this the raw cardReceive path,
    // which drops RF_DEACTIVATE_NTF without restarting discovery, leaves the
    // emulated card dormant after the first activation — exactly the "worked
    // once, can't reproduce" symptom.
    if (_cardActivitySinceReArm &&
        millis() - _lastCardActivity >= REARM_IDLE_MS) {
      LOG_INFO("card: terminal fuera del campo; re-armando discovery de emulación");
      if (!_nfc.cardReArm()) {
        LOG_WARN("card: re-arm de emulación falló");
      }
      _cardActivitySinceReArm = false;
      _lastCardActivity = millis();
    }
    return;  // no command from the terminal yet
  }

  // A frame came back from the PN7150: the terminal activated the emulated card
  // and the RF front-end is alive. Record the activity (so an idle gap later
  // triggers the re-arm above) and log even an empty frame, so RF activation is
  // observable *before* the first real APDU (DEBUG_card_emulation_no_rf_activation.md §4.4).
  _lastCardActivity = millis();
  _cardActivitySinceReArm = true;
  if (cmdLen == 0) {
    LOG_INFO("card: activación RF sin APDU (cardReceive frame vacío)");
    return;
  }
  Log::hex(LogLevel::Debug, "C<- term cmd:", cmd, cmdLen);
  emitCapture("cmd", cmd, cmdLen);

  // The command's content is terminal -> card, i.e. READER-tagged (see the
  // data-source semantics in the header). Forward it to our reader peer as a
  // CONTINUATION: with the real NFCGate app peer, an INITIAL-tagged frame is
  // fed to its daemon config parser (NfcManager.applyData's isInitial() branch)
  // instead of being transceived to the physical card — which crashes the app
  // and stalls the relay (Camino B2). Only the reader's one-off tag config is
  // ever INITIAL, and we never produce that.
  if (!_link.send(NfcSource::READER, cmd, cmdLen, NfcType::CONTINUATION)) {
    LOG_ERROR("RelayEngine: failed to forward terminal command");
    return;
  }
  // Confirm at INFO that a real terminal command was captured AND the link send
  // reported success — so we can tell "RF never activated" apart from "forwarded
  // but the server/peer never acted on it".
  if (Log::enabled(LogLevel::Info)) {
    String m = "card: comando de ";
    m += (int)cmdLen;
    m += " B capturado y reenviado al peer (send ok)";
    Log::line(LogLevel::Info, m);
  }
  _awaitingResponse = true;
  _awaitStart = millis();
}

void RelayEngine::cardHandleResponse(const NfcData &nfc) {
  const size_t respLen = nfc.data.size;
  if (respLen == 0) {
    return;  // nothing to inject
  }
  if (respLen > RELAY_MAX_RESP) {
    LOG_WARN("RelayEngine: response APDU too long for card path, dropping");
    return;
  }
  Log::hex(LogLevel::Debug, "C-> term resp:", nfc.data.bytes, respLen);
  emitCapture("resp", nfc.data.bytes, respLen);

  // nfc.data.bytes is const here; cardSend takes a non-const buffer, so copy
  // into a local scratch to inject to the terminal. cardSend fragments across
  // NCI packets when respLen > 255 (EMV records), so no truncating cast here.
  uint8_t resp[RELAY_MAX_RESP];
  memcpy(resp, nfc.data.bytes, respLen);
  _nfc.cardSend(resp, (uint16_t)respLen);

  _awaitingResponse = false;
  _awaitTimeouts = 0;  // a full round-trip proves the link is alive
  _relayed++;
}

void RelayEngine::emitCapture(const char *dir, const uint8_t *data, size_t len) {
  // A copy of one relayed APDU for the host-side pcap writer. Off the hot path:
  // guarded so it costs nothing (no serial traffic) while capture is disabled.
  if (_captureOut == nullptr || len == 0) {
    return;
  }
  static const char kHexDigits[] = "0123456789abcdef";
  _captureOut->print(":apdu ");
  _captureOut->print(dir);
  _captureOut->print(' ');
  _captureOut->print(millis());  // device ground-truth timestamp (ms)
  _captureOut->print(' ');
  for (size_t i = 0; i < len; ++i) {
    const uint8_t b = data[i];
    _captureOut->write(kHexDigits[b >> 4]);
    _captureOut->write(kHexDigits[b & 0x0F]);
  }
  _captureOut->print('\n');
}
