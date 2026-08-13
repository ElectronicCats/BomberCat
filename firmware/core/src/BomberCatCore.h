/**
 * BomberCatCore - umbrella header
 *
 * Single include that pulls in the reusable BomberCat relay building blocks.
 * Sketches may include this or the individual module headers directly.
 *
 * Modules present (NFCGATE_PLAN.md Fases 2-6):
 *   - Log        : level-gated logging over a Stream
 *   - HexUtils   : hex formatting helpers
 *   - NfcController : PN7150 wrapper (reader / emulation)
 *   - ConfigStore   : persistent WiFi + NFCGate relay config (TDBStore)
 *   - NfcGateCodec  : NFCData/ServerData framing (Arduino-free, host-testable)
 *   - NfcGateLink   : TCP transport to nfcgate-server over an Arduino Client
 *   - RelayEngine   : NfcController + NfcGateLink glue (session handshake +
 *                     READER- and CARD-role relay loops)
 *   - SerialControl : line-based control REPL for the Python CLI (config/run/
 *                     status over USB-serial; no APDUs on the wire)
 *
 * Distributed as-is; no warranty is given.
 */
#ifndef BOMBERCAT_CORE_H
#define BOMBERCAT_CORE_H

#include "ConfigStore.h"
#include "HexUtils.h"
#include "Log.h"
#include "NfcController.h"
#include "NfcGateLink.h"
#include "RelayEngine.h"
#include "SerialControl.h"

// NfcGateLink.h pulls in NfcGateCodec.h, which exposes the vendored NFCGate
// protobuf types (NFCData / ServerData) generated in Fase 1 along with short
// aliases (NfcData / ServerData / NfcSource / NfcOpcode).

#endif  // BOMBERCAT_CORE_H
