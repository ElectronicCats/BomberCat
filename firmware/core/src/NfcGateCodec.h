/**
 * BomberCatCore - NfcGateCodec
 *
 * Arduino-free codec for the NFCGate wire protocol (protocol @804fa9a,
 * server 4d32cc1 — see proto/UPSTREAM.md). It knows nothing about sockets or
 * WiFi; it only turns APDUs into framed bytes and back. NfcGateLink drives the
 * actual IO over an Arduino Client; keeping the codec here (no <Arduino.h>) lets
 * it be unit-tested on a host against the real server (tools/testserver/).
 *
 * Wire format (asymmetric framing):
 *   client -> server : [4B length BE][1B session][payload]
 *   server -> client : [4B length BE][payload]
 *   payload = ServerData{ opcode, data = NFCData{...} serialized }
 *
 * Short aliases are provided for the very long generated nanopb type names.
 *
 * Distributed as-is; no warranty is given.
 */
#ifndef BOMBERCAT_CORE_NFCGATECODEC_H
#define BOMBERCAT_CORE_NFCGATECODEC_H

#include <stddef.h>
#include <stdint.h>

#include "proto/c2c.pb.h"
#include "proto/c2s.pb.h"

// --- Short aliases over the fully-qualified nanopb symbols -----------------
typedef de_tu_darmstadt_seemoo_nfcgate_network_c2c_NFCData NfcData;
typedef de_tu_darmstadt_seemoo_nfcgate_network_c2s_ServerData ServerData;

// NFCData.data_source: which end produced the APDU (see UPSTREAM.md mapping).
enum class NfcSource : uint8_t {
  READER = de_tu_darmstadt_seemoo_nfcgate_network_c2c_NFCData_DataSource_READER,
  CARD = de_tu_darmstadt_seemoo_nfcgate_network_c2c_NFCData_DataSource_CARD,
};

// NFCData.data_type: first frame vs. a continuation.
enum class NfcType : uint8_t {
  INITIAL = de_tu_darmstadt_seemoo_nfcgate_network_c2c_NFCData_DataType_INITIAL,
  CONTINUATION =
      de_tu_darmstadt_seemoo_nfcgate_network_c2c_NFCData_DataType_CONTINUATION,
};

// ServerData.opcode: the minimal relay only needs PSH.
enum class NfcOpcode : uint8_t {
  PSH = de_tu_darmstadt_seemoo_nfcgate_network_c2s_ServerData_Opcode_OP_PSH,
  SYN = de_tu_darmstadt_seemoo_nfcgate_network_c2s_ServerData_Opcode_OP_SYN,
  ACK = de_tu_darmstadt_seemoo_nfcgate_network_c2s_ServerData_Opcode_OP_ACK,
  FIN = de_tu_darmstadt_seemoo_nfcgate_network_c2s_ServerData_Opcode_OP_FIN,
};

// Max serialized ServerData (payload) size, from the generated headers. A
// server->client payload never exceeds this, so it bounds the receive buffer.
static const size_t NFCGATE_MAX_PAYLOAD =
    de_tu_darmstadt_seemoo_nfcgate_network_c2s_ServerData_size;

// Max framed client->server bytes: 4B length + 1B session + payload.
static const size_t NFCGATE_MAX_FRAME = 5 + NFCGATE_MAX_PAYLOAD;

namespace NfcGateCodec {

// Populate `nfc` with one APDU. Returns false if `len` exceeds the field's
// static capacity (NFCData.data max_size). `timestamp` is Unix millis; the
// server treats NFCData as opaque so 0 is acceptable.
bool makeNfcData(NfcData &nfc, NfcSource source, NfcType type,
                 const uint8_t *apdu, size_t len, int64_t timestamp);

// Encode a complete client->server frame ([4B len BE][1B session][payload])
// into `out`. The payload is a ServerData{opcode=op, data=nfc-serialized}.
// Returns the number of bytes written, or 0 on error (encode failure or
// insufficient capacity).
size_t encodeFrame(uint8_t session, NfcOpcode op, const NfcData &nfc,
                   uint8_t *out, size_t outCap);

// Encode a data-less control frame ([4B len BE][1B session][payload]) whose
// payload is a ServerData{opcode=op} with NO embedded NFCData — matching the
// NFCGate app's sendServer(op, null) used for the SYN/ACK/FIN session handshake
// (see NetworkManager.java). `op` must NOT be PSH: OP_PSH is the proto3 default
// (0), so a data-less PSH would serialize to a 0-length payload, which the
// server reads as a disconnect. Returns bytes written, or 0 on error.
size_t encodeControlFrame(uint8_t session, NfcOpcode op, uint8_t *out,
                          size_t outCap);

// Decode a server->client payload (already de-framed — no length/session) into
// `sd` and its embedded `nfc`. Returns false on any protobuf decode error.
bool decodeServerData(const uint8_t *payload, size_t len, ServerData &sd,
                      NfcData &nfc);

}  // namespace NfcGateCodec

#endif  // BOMBERCAT_CORE_NFCGATECODEC_H
