/**
 * BomberCatCore - RelayEngine implementation.
 *
 * Distributed as-is; no warranty is given.
 */
#include "RelayEngine.h"

#include "Log.h"

namespace {
// NFCGate NCI-config option type bytes (app nfc/config/OptionType.java, commit
// 35f73ee). These are the TLV `type` tags the app's daemon parser expects.
enum : uint8_t {
  LA_BIT_FRAME_SDD = 0x30,    // ATQA byte 0
  LA_PLATFORM_CONFIG = 0x31,  // ATQA byte 1
  LA_SEL_INFO = 0x32,         // SAK
  LA_NFCID1 = 0x33,           // UID / NFCID1
  LI_A_RATS_TB1 = 0x58,       // ATS TB(1): FWI / SFGI
  LI_A_HIST_BY = 0x59,        // ATS historical bytes
  LI_A_RATS_TC1 = 0x5C,       // ATS TC(1): NAD / CID support
};

// Append one [type][len][value...] TLV (ConfigBuilder.build() wire format:
// type, 1-byte length, value). Returns the new offset, or 0 on overflow so the
// caller can bail.
size_t pushTlv(uint8_t *out, size_t cap, size_t off, uint8_t type,
               const uint8_t *val, uint8_t len) {
  if (off + 2u + len > cap) return 0;
  out[off++] = type;
  out[off++] = len;
  memcpy(out + off, val, len);
  return off + len;
}

size_t pushTlv1(uint8_t *out, size_t cap, size_t off, uint8_t type, uint8_t v) {
  return pushTlv(out, cap, off, type, &v, 1);
}

// Serialize the currently-activated NFC-A/ISO-DEP tag's parameters into the
// NFCGate NCI-config TLV stream the app's daemon (NfcManager.applyData ->
// beginSetConfig) expects. Mirrors NfcAReader.getConfig() + IsoDepReader
// .getConfig()/parseAtsRes() from the pinned app. Returns the byte count, or 0
// if no usable NFC-A tag is activated.
size_t buildTagConfig(RemoteDevice &dev, uint8_t *out, size_t cap) {
  const uint8_t *uid = dev.getNFCID();
  const uint8_t uidLen = dev.getNFCIDLen();
  const uint8_t *atqa = dev.getSensRes();  // 2 bytes for NFC-A
  const uint8_t atqaLen = dev.getSensResLen();
  const uint8_t *sak = dev.getSelRes();
  const uint8_t sakLen = dev.getSelResLen();
  if (uidLen == 0 || atqaLen < 2 || sakLen < 1) {
    return 0;  // not an NFC-A tag: nothing the app's Listen-A config can carry
  }

  // Core NFC-A anticollision. This alone is enough for the peer to present an
  // ISO-DEP target and let the terminal route SELECT PPSE. ATQA is split into
  // its two bytes exactly as the app does.
  size_t off = 0;
  off = pushTlv1(out, cap, off, LA_BIT_FRAME_SDD, atqa[0]);
  if (off) off = pushTlv1(out, cap, off, LA_PLATFORM_CONFIG, atqa[1]);
  if (off) off = pushTlv1(out, cap, off, LA_SEL_INFO, sak[0]);
  if (off) off = pushTlv(out, cap, off, LA_NFCID1, uid, uidLen);
  if (!off) return 0;

  // ISO-DEP (Type 4 / EMV): forward the ATS-derived Listen params so the
  // emulated card answers RATS like the physical card. ATS layout (ISO 14443-4):
  //   [TL] T0 [TA(1)] [TB(1)] [TC(1)] hist...
  // The app's parseAtsRes() starts at T0, so detect and skip a leading TL byte
  // (TL == total ATS length is its defining property).
  //
  // ⚠ HARDWARE-VERIFY: whether the PN7150's getRats() includes the leading TL
  // byte is the one thing not confirmed on device. The TL==len detection below
  // handles both; if a real card's ATS still mis-parses, log the raw getRats()
  // bytes and adjust. The four core options above do NOT depend on this.
  if (dev.getProtocol() == PROT_ISODEP && dev.getRatsLen() >= 2) {
    const uint8_t *ats = dev.getRats();
    const uint8_t atsLen = dev.getRatsLen();
    uint8_t i = (ats[0] == atsLen) ? 1 : 0;  // skip TL if present
    const uint8_t t0 = ats[i++];
    const uint8_t TA_P = 0x10, TB_P = 0x20, TC_P = 0x40;  // T0 presence bits
    if ((t0 & TA_P) && i < atsLen) i++;  // TA(1) = bit rate; skip (default 106k)
    if ((t0 & TB_P) && i < atsLen)
      off = pushTlv1(out, cap, off, LI_A_RATS_TB1, ats[i++]);
    if (off && (t0 & TC_P) && i < atsLen)
      off = pushTlv1(out, cap, off, LI_A_RATS_TC1, ats[i++]);
    if (off && i < atsLen)
      off = pushTlv(out, cap, off, LI_A_HIST_BY, ats + i, atsLen - i);
    if (!off) return 0;
  }
  return off;
}
}  // namespace

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
  _initialSent = false;
  _initialAttemptAt = 0;

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
      LOG_INFO(!_peerReady
                   ? "reader: vivo, sin peer aun"
                   : (!_initialSent
                          ? "reader: vivo, peer presente, enviando trama INITIAL "
                            "(coloca la tarjeta sobre el reader)"
                          : "reader: vivo, peer presente, esperando comando del peer"));
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

  // READER role: once the peer is present, emit the one-off INITIAL tag-config
  // frame (Camino B1: a rooted NFCGate emulator peer needs it to present our
  // physical card to its terminal). Inert for Camino A/B2 — a BomberCat card peer
  // ignores INITIAL (see handleFrame's PSH guard). emitInitialConfig() is
  // self-throttled and a no-op once sent.
  if (_cfg.roleEnum() == RelayRole::READER && _peerReady && !_initialSent) {
    emitInitialConfig();
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
      // Re-arm the one-off INITIAL for the next peer that joins this session, so
      // a reconnecting emulator gets the tag config again.
      _initialSent = false;
      _initialAttemptAt = 0;
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

bool RelayEngine::emitInitialConfig() {
  // Throttle re-attempts: while no card is on the reader yet, waitForTag() blocks
  // up to 500 ms; without this we would spin that probe every loop() and starve
  // frame draining. Once a card is present, waitForTag returns fast and this
  // fires on the first attempt.
  const unsigned long nowMs = millis();
  if (_initialAttemptAt != 0 && nowMs - _initialAttemptAt < INITIAL_RETRY_MS) {
    return false;
  }
  _initialAttemptAt = nowMs;

  // Activate the physical card so remoteDevice holds its real UID/SAK/ATQA/ATS.
  // Keep _tagReady so readerHandleCommand reuses this activation for the first
  // command (the boundary self-heal re-activates if the session goes stale in the
  // idle gap before the terminal drives SELECT PPSE).
  if (!_tagReady) {
    if (!_nfc.waitForTag(500)) {
      LOG_DEBUG("reader: sin tarjeta para la trama INITIAL; reintentare");
      return false;
    }
    _tagReady = true;
    LOG_DEBUG("reader: tarjeta activada (para trama INITIAL)");
  }

  uint8_t cfg[96];
  const size_t cfgLen = buildTagConfig(_nfc.raw().remoteDevice, cfg, sizeof(cfg));
  if (cfgLen == 0) {
    // Not NFC-A (or no usable params). A rooted emulator peer can't present a tag
    // without this, but a BomberCat peer doesn't need it — so don't spin: mark
    // sent and let the normal command path proceed.
    LOG_WARN("reader: sin config de tag NFC-A; no envio INITIAL");
    _initialSent = true;
    return false;
  }

  Log::hex(LogLevel::Debug, "reader INITIAL cfg:", cfg, cfgLen);
  if (!_link.send(NfcSource::CARD, cfg, cfgLen, NfcType::INITIAL)) {
    LOG_ERROR("reader: fallo al enviar la trama INITIAL; reintentare");
    return false;  // transient link issue: retry next window
  }
  _initialSent = true;
  LOG_INFO("reader: trama INITIAL (config de tag) enviada al peer");
  return true;
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
