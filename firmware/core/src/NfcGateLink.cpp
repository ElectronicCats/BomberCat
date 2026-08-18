/**
 * BomberCatCore - NfcGateLink implementation.
 *
 * Distributed as-is; no warranty is given.
 */
#include "NfcGateLink.h"

#include "Log.h"

bool NfcGateLink::connect(const char *host, uint16_t port, uint8_t session) {
  _session = session;
  resetRx();
  if (_c.connect(host, port) != 1) {
    LOG_ERROR("NfcGateLink: connect failed");
    return false;
  }
  LOG_INFO("NfcGateLink: connected");
  return true;
}

void NfcGateLink::stop() {
  _c.stop();
  resetRx();
}

void NfcGateLink::resetRx() {
  _hdrHave = 0;
  _haveHdr = false;
  _need = 0;
  _fill = 0;
}

bool NfcGateLink::send(NfcSource source, const uint8_t *apdu, size_t len,
                       NfcType type, int64_t timestamp) {
  NfcData nfc;
  if (!NfcGateCodec::makeNfcData(nfc, source, type, apdu, len, timestamp)) {
    LOG_ERROR("NfcGateLink: APDU too large");
    return false;
  }
  return sendRaw(NfcOpcode::PSH, nfc);
}

bool NfcGateLink::sendRaw(NfcOpcode op, const NfcData &nfc) {
  if (!_c.connected()) {
    return false;
  }
  size_t n = NfcGateCodec::encodeFrame(_session, op, nfc, _tx, sizeof(_tx));
  if (n == 0) {
    LOG_ERROR("NfcGateLink: encode failed");
    return false;
  }
  size_t written = _c.write(_tx, n);
  if (written != n) {
    LOG_ERROR("NfcGateLink: short write");
    return false;
  }
  return true;
}

bool NfcGateLink::sendControl(NfcOpcode op) {
  if (!_c.connected()) {
    return false;
  }
  size_t n = NfcGateCodec::encodeControlFrame(_session, op, _tx, sizeof(_tx));
  if (n == 0) {
    LOG_ERROR("NfcGateLink: control encode failed");
    return false;
  }
  size_t written = _c.write(_tx, n);
  if (written != n) {
    LOG_ERROR("NfcGateLink: short write (control)");
    return false;
  }
  return true;
}

int NfcGateLink::poll(ServerData &sd, NfcData &nfc) {
  if (!_c.connected()) {
    return -1;
  }

  // Phase 1: accumulate the 4-byte big-endian length header. Read whatever of
  // the header is still missing in ONE bulk _c.read() instead of byte-by-byte
  // (Fase F, LATENCIA_OPTIMIZACION.md §2 H5). On the BomberCat the WiFiClient is
  // WiFiNINA: the ESP32-WROOM is an SPI coprocessor, so *every* _c.read() and
  // _c.available() is a full SPI command round-trip (command + slave-ready
  // handshake) to it. The old byte-at-a-time loop cost up to 4 read() + 4
  // available() calls per frame; since Fase E made the server ship header+payload
  // as ONE TCP segment, the whole header is normally already buffered on the
  // ESP32, so this collapses to a single available()+read() — cutting ~6 SPI
  // transactions off every server->board frame (~18/txn per board) with zero RF
  // risk (pure transport; never touches I2C/PN7150). Partial-header arrival is
  // still handled: read() takes only the bytes present and _hdrHave carries the
  // count across poll() calls exactly as before.
  if (!_haveHdr && _c.available() > 0) {
    int n = _c.read(&_hdr[_hdrHave], (size_t)(4 - _hdrHave));
    if (n > 0) {
      _hdrHave += (uint8_t)n;
      if (_hdrHave == 4) {
        _need = ((uint32_t)_hdr[0] << 24) | ((uint32_t)_hdr[1] << 16) |
                ((uint32_t)_hdr[2] << 8) | (uint32_t)_hdr[3];
        _haveHdr = true;
        _fill = 0;
        if (_need > sizeof(_rx)) {
          LOG_ERROR("NfcGateLink: oversized frame, dropping link");
          stop();
          return -1;
        }
      }
    }
  }
  if (!_haveHdr) {
    return 0;
  }

  // Phase 2: accumulate the payload.
  while (_fill < _need && _c.available() > 0) {
    size_t want = _need - _fill;
    int n = _c.read(&_rx[_fill], want);
    if (n <= 0) {
      break;
    }
    _fill += (uint32_t)n;
  }
  if (_fill < _need) {
    return 0;
  }

  bool ok = NfcGateCodec::decodeServerData(_rx, _need, sd, nfc);
  resetRx();
  if (!ok) {
    LOG_ERROR("NfcGateLink: decode failed");
    return -1;
  }
  return 1;
}

int NfcGateLink::receive(NfcSource &source, uint8_t *buf, size_t bufCap,
                         size_t &outLen) {
  ServerData sd;
  NfcData nfc;
  int r = poll(sd, nfc);
  if (r != 1) {
    return r;
  }
  source = (NfcSource)nfc.data_source;
  size_t n = nfc.data.size;
  if (n > bufCap) {
    n = bufCap;
  }
  if (n > 0) {
    memcpy(buf, nfc.data.bytes, n);
  }
  outLen = n;
  return 1;
}
