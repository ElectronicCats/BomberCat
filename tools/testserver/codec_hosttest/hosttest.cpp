// hosttest.cpp — exercise the firmware NfcGateCodec against a live
// nfcgate-server, with no RF and no Arduino runtime.
//
// It compiles the *actual* firmware codec (firmware/core/src/NfcGateCodec.cpp)
// plus the vendored nanopb, then plays the same reader<->card loopback as
// tools/testserver/relay_smoketest.py — but building/parsing every frame with
// the code the RP2040 will run. This is the "probado contra el servidor"
// verification for NfcGateLink's wire format (docs/NFCGATE_PLAN.md Fase 3 / §6 mock).
//
//   client -> server : [4B len BE][1B session][payload]   (encodeFrame emits this)
//   server -> client : [4B len BE][payload]               (we de-frame, then decode)
//
// Build & run:  tools/testserver/codec_hosttest/build_and_run.sh [host] [port]

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "NfcGateCodec.h"

namespace {

const uint8_t kSession = 0x2A;  // any non-zero byte; both peers must match

int connectTcp(const char* host, const char* port) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  if (getaddrinfo(host, port, &hints, &res) != 0 || res == nullptr) {
    fprintf(stderr, "getaddrinfo(%s:%s) failed\n", host, port);
    return -1;
  }
  int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd >= 0 && connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
    perror("connect");
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  return fd;
}

bool writeAll(int fd, const uint8_t* buf, size_t len) {
  size_t off = 0;
  while (off < len) {
    ssize_t n = write(fd, buf + off, len - off);
    if (n <= 0) return false;
    off += (size_t)n;
  }
  return true;
}

bool readAll(int fd, uint8_t* buf, size_t len) {
  size_t off = 0;
  while (off < len) {
    ssize_t n = read(fd, buf + off, len - off);
    if (n <= 0) return false;
    off += (size_t)n;
  }
  return true;
}

// Send one APDU using the firmware codec (full framed client->server frame).
bool sendApdu(int fd, NfcSource source, const uint8_t* apdu, size_t len) {
  NfcData nfc;
  if (!NfcGateCodec::makeNfcData(nfc, source, NfcType::INITIAL, apdu, len, 0)) {
    fprintf(stderr, "makeNfcData failed\n");
    return false;
  }
  uint8_t frame[NFCGATE_MAX_FRAME];
  size_t n = NfcGateCodec::encodeFrame(kSession, NfcOpcode::PSH, nfc, frame,
                                       sizeof(frame));
  if (n == 0) {
    fprintf(stderr, "encodeFrame failed\n");
    return false;
  }
  return writeAll(fd, frame, n);
}

// Receive one server->client frame and decode it with the firmware codec.
bool recvApdu(int fd, NfcSource* source, uint8_t* out, size_t outCap,
              size_t* outLen) {
  uint8_t hdr[4];
  if (!readAll(fd, hdr, 4)) return false;
  uint32_t plen = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) |
                  ((uint32_t)hdr[2] << 8) | (uint32_t)hdr[3];
  if (plen > NFCGATE_MAX_PAYLOAD) {
    fprintf(stderr, "payload too big: %u\n", plen);
    return false;
  }
  uint8_t payload[NFCGATE_MAX_PAYLOAD];
  if (!readAll(fd, payload, plen)) return false;

  ServerData sd;
  NfcData nfc;
  if (!NfcGateCodec::decodeServerData(payload, plen, sd, nfc)) {
    fprintf(stderr, "decodeServerData failed\n");
    return false;
  }
  *source = (NfcSource)nfc.data_source;
  size_t n = nfc.data.size;
  if (n > outCap) n = outCap;
  memcpy(out, nfc.data.bytes, n);
  *outLen = n;
  return true;
}

// Send a data-less control frame (SYN/ACK/FIN) using the firmware codec.
bool sendControl(int fd, NfcOpcode op) {
  uint8_t frame[NFCGATE_MAX_FRAME];
  size_t n = NfcGateCodec::encodeControlFrame(kSession, op, frame, sizeof(frame));
  if (n == 0) {
    fprintf(stderr, "encodeControlFrame failed\n");
    return false;
  }
  return writeAll(fd, frame, n);
}

// Receive one server->client frame and return its decoded ServerData opcode and
// the embedded NFCData size (0 for a control frame).
bool recvServerData(int fd, NfcOpcode* op, size_t* nfcLen) {
  uint8_t hdr[4];
  if (!readAll(fd, hdr, 4)) return false;
  uint32_t plen = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) |
                  ((uint32_t)hdr[2] << 8) | (uint32_t)hdr[3];
  if (plen > NFCGATE_MAX_PAYLOAD) {
    fprintf(stderr, "payload too big: %u\n", plen);
    return false;
  }
  uint8_t payload[NFCGATE_MAX_PAYLOAD];
  if (!readAll(fd, payload, plen)) return false;

  ServerData sd;
  NfcData nfc;
  if (!NfcGateCodec::decodeServerData(payload, plen, sd, nfc)) {
    fprintf(stderr, "decodeServerData failed\n");
    return false;
  }
  *op = (NfcOpcode)sd.opcode;
  *nfcLen = nfc.data.size;
  return true;
}

std::string hex(const uint8_t* p, size_t n) {
  static const char* d = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; i++) {
    s += d[p[i] >> 4];
    s += d[p[i] & 0xF];
  }
  return s;
}

bool eq(const uint8_t* a, size_t an, const uint8_t* b, size_t bn) {
  return an == bn && memcmp(a, b, an) == 0;
}

}  // namespace

int main(int argc, char** argv) {
  const char* host = argc > 1 ? argv[1] : "127.0.0.1";
  const char* port = argc > 2 ? argv[2] : "5566";
  printf("Connecting to %s:%s (session 0x%02X)\n", host, port, kSession);

  int reader = connectTcp(host, port);
  int card = connectTcp(host, port);
  if (reader < 0 || card < 0) {
    fprintf(stderr,
            "Could not reach server. Start it with tools/testserver/run.sh\n");
    return 1;
  }

  int rc = 0;

  // --- Session handshake (SYN/ACK), as the NFCGate app / RelayEngine do. ---
  // Sending any frame registers a client with the session, so the SYN doubles
  // as registration. Reader registers first; then the card's SYN is forwarded
  // to the reader, and the reader's ACK back to the card.
  NfcOpcode op;
  size_t nlen = 0;
  if (!sendControl(reader, NfcOpcode::SYN)) return 1;  // registers reader
  usleep(200 * 1000);
  if (!sendControl(card, NfcOpcode::SYN)) return 1;    // registers card + fwd
  if (!recvServerData(reader, &op, &nlen)) return 1;
  if (op == NfcOpcode::SYN && nlen == 0) {
    printf("[OK] handshake   reader received peer SYN (no data)\n");
  } else {
    printf("[FAIL] handshake opcode=%d nfcLen=%zu\n", (int)op, nlen);
    rc = 1;
  }
  if (!sendControl(reader, NfcOpcode::ACK)) return 1;  // reader ACKs the card
  if (!recvServerData(card, &op, &nlen)) return 1;
  if (op == NfcOpcode::ACK && nlen == 0) {
    printf("[OK] handshake   card received peer ACK (no data)\n");
  } else {
    printf("[FAIL] handshake opcode=%d nfcLen=%zu\n", (int)op, nlen);
    rc = 1;
  }

  uint8_t buf[NFCGATE_MAX_PAYLOAD];
  size_t len = 0;
  NfcSource src;

  // reader -> card : SELECT PPSE
  const uint8_t ppse[] = {0x00, 0xA4, 0x04, 0x00, 0x0E, 0x32, 0x50, 0x41,
                          0x59, 0x2E, 0x53, 0x59, 0x53, 0x2E, 0x44, 0x44,
                          0x46, 0x30, 0x31, 0x00};
  if (!sendApdu(reader, NfcSource::READER, ppse, sizeof(ppse))) return 1;
  if (!recvApdu(card, &src, buf, sizeof(buf), &len)) return 1;
  if (src == NfcSource::READER && eq(buf, len, ppse, sizeof(ppse))) {
    printf("[OK] reader->card  source=READER apdu=%s\n", hex(buf, len).c_str());
  } else {
    printf("[FAIL] reader->card apdu=%s\n", hex(buf, len).c_str());
    rc = 1;
  }

  // card -> reader : FCI response
  const uint8_t fci[] = {0x6F, 0x23, 0x84, 0x0E, 0x32, 0x50, 0x41, 0x59,
                         0x2E, 0x53, 0x59, 0x53, 0x2E, 0x44, 0x44, 0x46,
                         0x30, 0x31, 0xA5, 0x11, 0x90, 0x00};
  if (!sendApdu(card, NfcSource::CARD, fci, sizeof(fci))) return 1;
  if (!recvApdu(reader, &src, buf, sizeof(buf), &len)) return 1;
  if (src == NfcSource::CARD && eq(buf, len, fci, sizeof(fci))) {
    printf("[OK] card->reader  source=CARD apdu=%s\n", hex(buf, len).c_str());
  } else {
    printf("[FAIL] card->reader apdu=%s\n", hex(buf, len).c_str());
    rc = 1;
  }

  close(reader);
  close(card);
  printf(rc == 0 ? "\nCODEC HOST TEST PASSED\n" : "\nCODEC HOST TEST FAILED\n");
  return rc;
}
