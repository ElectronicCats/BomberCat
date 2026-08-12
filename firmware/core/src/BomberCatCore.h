/**
 * BomberCatCore - umbrella header
 *
 * Single include that pulls in the reusable BomberCat relay building blocks.
 * Sketches may include this or the individual module headers directly.
 *
 * Modules present in this phase (NFCGATE_PLAN.md Fase 2):
 *   - Log        : level-gated logging over a Stream
 *   - HexUtils   : hex formatting helpers
 *   - NfcController : PN7150 wrapper (reader / emulation)
 *   - ConfigStore   : persistent WiFi + NFCGate relay config (TDBStore)
 *
 * Coming in later phases: NfcGateLink (WiFi/TCP + protobuf framing),
 * RelayEngine, SerialControl.
 *
 * Distributed as-is; no warranty is given.
 */
#ifndef BOMBERCAT_CORE_H
#define BOMBERCAT_CORE_H

#include "ConfigStore.h"
#include "HexUtils.h"
#include "Log.h"
#include "NfcController.h"

// The vendored NFCGate protobuf types (NFCData / ServerData) generated in
// Fase 1. Included here so sketches get them via BomberCatCore.h once
// NfcGateLink lands; harmless to expose now.
#include "proto/c2c.pb.h"
#include "proto/c2s.pb.h"

#endif  // BOMBERCAT_CORE_H
