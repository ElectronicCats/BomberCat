# BomberCatCore

Reusable Arduino library that is the foundation for the NFCGate relay firmware
(see [`../../NFCGATE_PLAN.md`](../../NFCGATE_PLAN.md)). It is a **local library**:
`#include <BomberCatCore.h>` from any sketch in this repo, or include a single
module header directly.

Target: **Arduino Mbed OS RP2040** core (`mbed_rp2040`). The persistence layer
uses mbed `TDBStore` / `FlashIAPBlockDevice`, which are only available there.

## Modules (Fases 2-4)

| Header | Class / API | Responsibility |
|---|---|---|
| `Log.h` | `Log`, `LOG_ERROR/WARN/INFO/DEBUG` | Level-gated logging over a `Stream`. Replaces the scattered `if (debug) …` blocks. Levels are `LogLevel::None/Error/Warn/Info/Debug` (PascalCase: the PN7150 lib `#define ERROR`, so an all-caps enum member would be mangled by the preprocessor). |
| `HexUtils.h` | `HexUtils::toString`, `HexUtils::print` | Hex dump formatting (factored from `getHexRepresentation` / `printData`). |
| `NfcController.h` | `NfcController` | Wrapper over `Electroniccats_PN7150`: `beginReaderMode()`, `beginEmulationMode()`, `reset()`, `waitForTag()`, `readerTransceive()`, `cardReceive()` / `cardSend()`. Setup failures **return false** instead of hanging. |
| `ConfigStore.h` | `ConfigStore`, `RelayConfig`, `RelayRole` | Persistent WiFi + NFCGate relay config (SSID/pass, server host/port, session byte, role) on `TDBStore`. |
| `NfcGateCodec.h` | `NfcGateCodec::makeNfcData/encodeFrame/decodeServerData`; aliases `NfcData`, `ServerData`, `NfcSource`, `NfcType`, `NfcOpcode` | **Arduino-free** codec for the NFCGate wire format: builds/parses the length-prefixed `ServerData{NFCData}` frames. No sockets — pure bytes, so it is host-testable (see below). Short aliases tame the very long generated nanopb names. |
| `NfcGateLink.h` | `NfcGateLink` | TCP transport to `nfcgate-server` over an Arduino `Client&` (WiFiNINA `WiFiClient` on device). `connect()`, `send()`, `sendControl()` (SYN/ACK/FIN), non-blocking `poll()` / `receive()`. Owns the asymmetric framing (`[4B len BE][1B session][payload]` c→s); delegates protobuf to `NfcGateCodec`. WiFi *association* stays in the sketch. |
| `RelayEngine.h` | `RelayEngine` | Glue over `NfcController` + `NfcGateLink`: `begin()` (NFC bring-up + connect + `OP_SYN` session join), non-blocking `loop()`, `stop()` (`OP_FIN`). Owns the SYN/ACK handshake and the **READER**-role relay loop (command in → card transceive → response out). CARD role is a stub (Fase 5). No WiFiNINA dependency — WiFi stays in the sketch. |
| `FlashIAPLimits.h` | `mbed::getFlashIAPLimits()` | Computes the usable flash region past the sketch (vendored from the relay sketches). |
| `proto/…` | `NFCData`, `ServerData` | NFCGate wire messages, generated with nanopb in Fase 1. See [`../proto/UPSTREAM.md`](../proto/UPSTREAM.md). |
| `pb*.{h,c}` | nanopb runtime | Vendored nanopb 0.4.9.1 runtime (`pb.h`, `pb_common`, `pb_encode`, `pb_decode`), zlib-licensed — see `NANOPB_LICENSE.txt`. Kept flat in `src/` so the generated `proto/*.pb.h` resolve `#include <pb.h>` via the library's include path. |

Coming in later phases: CARD-role relay (Fase 5), `SerialControl`.

## Dependencies

- **Electronic Cats PN7150** (`Electroniccats_PN7150`) — install via Library
  Manager.
- **nanopb runtime** — **vendored** in `src/` (`pb*.{h,c}`), so no Library
  Manager entry is needed. (nanopb is *not* published to the Arduino Library
  Manager; it is only referenced there as a dependency name, which cannot be
  resolved — hence vendoring.) Every `.c`/`.cpp` under `src/` is compiled when
  the library is used, so this is required even before `NfcGateLink` exists.
- mbed `TDBStore` / `FlashIAPBlockDevice` — provided by the `mbed_rp2040` core.

## Building / verifying

There is no separate build step for the library. To smoke-test that it compiles,
open **File ▸ Examples ▸ BomberCatCore ▸ CoreSelfTest** (after making
`firmware/core` visible to the IDE, e.g. symlink it into your
`Arduino/libraries/`), select the **Electronic Cats BomberCat** board, and
compile (the ✓ button — no upload needed). Or with `arduino-cli`:

```sh
arduino-cli compile -b electroniccats:mbed_rp2040:bombercat \
  --library firmware/core \
  firmware/core/examples/CoreSelfTest
```

`CoreSelfTest` exercises every public symbol (including a `NfcGateCodec`
round-trip and `NfcGateLink` over a `WiFiClient`); it is a compile check, not a
functional relay. Fase 2 was verified building clean against
`electroniccats:mbed_rp2040` 2.0.0 + `Electronic Cats PN7150` 3.1.1 (~112 KB
flash / 45 KB RAM); re-run the command above after the Fase 3 additions.

### Host test for the wire format (no board, no RF)

`NfcGateLink`'s wire format is validated off-device by compiling the **actual**
`NfcGateCodec.cpp` + vendored nanopb with a host compiler and running a
reader↔card loopback against a local `nfcgate-server`:

```sh
tools/testserver/run.sh                       # terminal 1: server on :5566
tools/testserver/codec_hosttest/build_and_run.sh   # terminal 2: g++ + loopback
```

A green `CODEC HOST TEST PASSED` proves the bytes the RP2040 will emit are
accepted and relayed by the real server (and that server frames decode back to
the original APDUs). RF/PN7150 are not involved — this is the Fase 3 / §6 "mock"
verification. See [`../../tools/testserver/codec_hosttest/`](../../tools/testserver/codec_hosttest/).

## Notes on fidelity to the legacy sketches

`NfcController` mirrors the exact call sequences of `host_Relay_NFC` /
`client_Relay_NFC` with two deliberate differences, so host/client can migrate
onto it later:

1. NCI bring-up failures return `false` (the sketches did `while (1);`).
2. `readerTransceive()` sends then waits for **one** response frame with a
   timeout; it does not reproduce the legacy double-`cardModeReceive()` in
   `seekTrack2()`. Use `raw()` if you need a byte-for-byte legacy sequence.
