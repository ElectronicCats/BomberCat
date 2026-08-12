/**
 * BomberCatCore - NfcGateLink
 *
 * Transport for the NFCGate relay: an Arduino Client (WiFiNINA WiFiClient on the
 * BomberCat) plus the NfcGateCodec. It owns the TCP connection to nfcgate-server
 * and the length-prefixed framing; the protobuf encode/decode lives in
 * NfcGateCodec so it can be tested off-device.
 *
 * Taking a Client& (not a concrete WiFiClient) keeps this decoupled from
 * WiFiNINA and lets a test double drive it. WiFi *association* (WiFi.begin) is
 * the sketch's / RelayEngine's job; this class only does connect(host,port) and
 * the frame IO once the radio is up.
 *
 * Receiving is non-blocking: poll() drains whatever bytes are available and
 * returns a decoded frame only once a full one has arrived, so it fits straight
 * into a cooperative relay loop() with no blocking reads.
 *
 * Distributed as-is; no warranty is given.
 */
#ifndef BOMBERCAT_CORE_NFCGATELINK_H
#define BOMBERCAT_CORE_NFCGATELINK_H

#include <Arduino.h>
#include <Client.h>

#include "NfcGateCodec.h"

class NfcGateLink {
 public:
  // `client` must outlive this link (typically a global WiFiClient).
  explicit NfcGateLink(Client &client) : _c(client) {}

  // Open the TCP connection and adopt `session` (the 1-byte session id that
  // groups the two relay peers on the server). Returns true on success.
  bool connect(const char *host, uint16_t port, uint8_t session);

  bool connected() { return _c.connected(); }
  void stop();

  uint8_t session() const { return _session; }
  void setSession(uint8_t s) { _session = s; }

  // Send one APDU as an OP_PSH frame. `source` marks who produced it (READER or
  // CARD). Returns false if not connected, the APDU is too large, or the write
  // is short.
  bool send(NfcSource source, const uint8_t *apdu, size_t len,
            NfcType type = NfcType::INITIAL, int64_t timestamp = 0);

  // Send a pre-built NFCData with an explicit opcode (SYN/ACK/FIN or PSH).
  bool sendRaw(NfcOpcode op, const NfcData &nfc);

  // Non-blocking receive of one server->client frame. Returns:
  //    1  a full frame decoded into `sd` / `nfc`
  //    0  nothing complete yet (call again later)
  //   -1  connection lost or a malformed/oversized frame (link reset)
  int poll(ServerData &sd, NfcData &nfc);

  // Convenience over poll(): copies just the APDU bytes of a received frame.
  // Same return codes as poll(); on 1, `source`/`outLen` describe the APDU and
  // up to `bufCap` bytes are copied into `buf` (truncated if larger).
  int receive(NfcSource &source, uint8_t *buf, size_t bufCap, size_t &outLen);

 private:
  void resetRx();

  Client &_c;
  uint8_t _session = 0;

  // Receive state machine: 4-byte big-endian length header, then payload.
  uint8_t _hdr[4] = {0};
  uint8_t _hdrHave = 0;
  bool _haveHdr = false;
  uint32_t _need = 0;
  uint32_t _fill = 0;
  uint8_t _rx[NFCGATE_MAX_PAYLOAD];

  // Transmit scratch (built once per send()).
  uint8_t _tx[NFCGATE_MAX_FRAME];
};

#endif  // BOMBERCAT_CORE_NFCGATELINK_H
