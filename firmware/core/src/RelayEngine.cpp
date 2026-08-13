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
  _peerReady = false;
  _tagReady = false;
  _awaitingResponse = false;

  // 1. Bring up the PN7150 in the role's RF mode.
  const bool isReader = _cfg.roleEnum() == RelayRole::READER;
  const bool nfcOk = isReader ? _nfc.beginReaderMode() : _nfc.beginEmulationMode();
  if (!nfcOk) {
    LOG_ERROR("RelayEngine: NFC bring-up failed");
    _state = State::Error;
    return false;
  }
  LOG_INFO(isReader ? "RelayEngine: NFC reader mode ready"
                    : "RelayEngine: NFC emulation mode ready");

  // 2. Open the TCP link and adopt the session byte.
  if (!_link.connect(_cfg.server, _cfg.port, _cfg.session)) {
    LOG_ERROR("RelayEngine: link connect failed");
    _state = State::Error;
    return false;
  }

  // 3. Announce + register with the session (OP_SYN, as the NFCGate app does).
  //    The server only associates a client with a session once it sends a
  //    frame, so this is what makes us able to receive the peer's commands.
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

  // Make sure a physical card is activated in the field before transceiving.
  // We keep it activated across the transaction; on a transceive failure we
  // drop readiness so the next command re-activates (e.g. after card removal).
  if (!_tagReady) {
    if (_nfc.waitForTag(500)) {
      _tagReady = true;
      LOG_DEBUG("RelayEngine: card activated");
    } else {
      LOG_WARN("RelayEngine: no card in field, dropping command");
      return;
    }
  }

  uint8_t resp[RELAY_MAX_APDU];
  uint8_t respLen = 0;
  // nfc.data.bytes is const here; NfcController's API takes a non-const buffer
  // (it never writes the command), so copy into a local scratch to transceive.
  uint8_t cmd[RELAY_MAX_APDU];
  memcpy(cmd, nfc.data.bytes, cmdLen);

  if (!_nfc.readerTransceive(cmd, (uint8_t)cmdLen, resp, &respLen)) {
    LOG_WARN("RelayEngine: card transceive failed/timeout");
    _tagReady = false;  // force re-activation on the next command
    return;
  }

  Log::hex(LogLevel::Debug, "R-> resp:", resp, respLen);
  if (!_link.send(NfcSource::CARD, resp, respLen)) {
    LOG_ERROR("RelayEngine: failed to send response");
    return;
  }
  _relayed++;
}

void RelayEngine::cardPollTerminal() {
  // Strict request/response: only ask the terminal for a new command once the
  // previous one's response has been injected. The terminal itself won't issue
  // the next command until it is answered, so this just keeps us in lock-step
  // (and avoids forwarding a stray frame out of order).
  if (_awaitingResponse) {
    return;
  }

  uint8_t cmd[RELAY_MAX_APDU];
  uint8_t cmdLen = 0;
  if (!_nfc.cardReceive(cmd, &cmdLen)) {
    return;  // no command from the terminal yet
  }
  if (cmdLen == 0) {
    return;
  }
  Log::hex(LogLevel::Debug, "C<- term cmd:", cmd, cmdLen);

  // The command's content is terminal -> card, i.e. READER-tagged (see the
  // data-source semantics in the header). Forward it to our reader peer.
  if (!_link.send(NfcSource::READER, cmd, cmdLen)) {
    LOG_ERROR("RelayEngine: failed to forward terminal command");
    return;
  }
  _awaitingResponse = true;
}

void RelayEngine::cardHandleResponse(const NfcData &nfc) {
  const size_t respLen = nfc.data.size;
  if (respLen == 0) {
    return;  // nothing to inject
  }
  if (respLen > RELAY_MAX_APDU) {
    LOG_WARN("RelayEngine: response APDU too long for card path, dropping");
    return;
  }
  Log::hex(LogLevel::Debug, "C-> term resp:", nfc.data.bytes, respLen);

  // nfc.data.bytes is const here; cardSend takes a non-const buffer, so copy
  // into a local scratch to inject to the terminal.
  uint8_t resp[RELAY_MAX_APDU];
  memcpy(resp, nfc.data.bytes, respLen);
  _nfc.cardSend(resp, (uint8_t)respLen);

  _awaitingResponse = false;
  _relayed++;
}
