/**
 * BomberCatCore - NfcGateCodec implementation.
 *
 * Distributed as-is; no warranty is given.
 */
#include "NfcGateCodec.h"

#include <string.h>

#include "pb_decode.h"
#include "pb_encode.h"

namespace NfcGateCodec {

bool makeNfcData(NfcData &nfc, NfcSource source, NfcType type,
                 const uint8_t *apdu, size_t len, int64_t timestamp) {
  if (len > sizeof(nfc.data.bytes)) {
    return false;
  }
  nfc = NfcData de_tu_darmstadt_seemoo_nfcgate_network_c2c_NFCData_init_zero;
  nfc.data_source =
      (de_tu_darmstadt_seemoo_nfcgate_network_c2c_NFCData_DataSource)source;
  nfc.data_type =
      (de_tu_darmstadt_seemoo_nfcgate_network_c2c_NFCData_DataType)type;
  nfc.timestamp = timestamp;
  if (len > 0 && apdu != nullptr) {
    memcpy(nfc.data.bytes, apdu, len);
  }
  nfc.data.size = (pb_size_t)len;
  return true;
}

size_t encodeFrame(uint8_t session, NfcOpcode op, const NfcData &nfc,
                   uint8_t *out, size_t outCap) {
  // 4B length + 1B session precede the payload.
  if (outCap < 5) {
    return 0;
  }

  // Serialize the inner NFCData into ServerData.data.
  ServerData sd =
      de_tu_darmstadt_seemoo_nfcgate_network_c2s_ServerData_init_zero;
  sd.opcode =
      (de_tu_darmstadt_seemoo_nfcgate_network_c2s_ServerData_Opcode)op;
  pb_ostream_t ns = pb_ostream_from_buffer(sd.data.bytes, sizeof(sd.data.bytes));
  if (!pb_encode(&ns,
                 de_tu_darmstadt_seemoo_nfcgate_network_c2c_NFCData_fields,
                 &nfc)) {
    return 0;
  }
  sd.data.size = (pb_size_t)ns.bytes_written;

  // Serialize ServerData directly into the frame body (after len + session).
  pb_ostream_t ps = pb_ostream_from_buffer(out + 5, outCap - 5);
  if (!pb_encode(&ps,
                 de_tu_darmstadt_seemoo_nfcgate_network_c2s_ServerData_fields,
                 &sd)) {
    return 0;
  }
  uint32_t plen = (uint32_t)ps.bytes_written;

  out[0] = (uint8_t)(plen >> 24);
  out[1] = (uint8_t)(plen >> 16);
  out[2] = (uint8_t)(plen >> 8);
  out[3] = (uint8_t)(plen);
  out[4] = session;
  return (size_t)plen + 5;
}

bool decodeServerData(const uint8_t *payload, size_t len, ServerData &sd,
                      NfcData &nfc) {
  sd = de_tu_darmstadt_seemoo_nfcgate_network_c2s_ServerData_init_zero;
  pb_istream_t is = pb_istream_from_buffer(payload, len);
  if (!pb_decode(&is,
                 de_tu_darmstadt_seemoo_nfcgate_network_c2s_ServerData_fields,
                 &sd)) {
    return false;
  }

  nfc = de_tu_darmstadt_seemoo_nfcgate_network_c2c_NFCData_init_zero;
  pb_istream_t ns = pb_istream_from_buffer(sd.data.bytes, sd.data.size);
  if (!pb_decode(&ns,
                 de_tu_darmstadt_seemoo_nfcgate_network_c2c_NFCData_fields,
                 &nfc)) {
    return false;
  }
  return true;
}

}  // namespace NfcGateCodec
