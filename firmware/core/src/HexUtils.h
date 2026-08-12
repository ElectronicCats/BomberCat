/**
 * BomberCatCore - HexUtils
 *
 * Hex formatting helpers, factored out of the duplicated getHexRepresentation()
 * / printData() found in host_Relay_NFC and client_Relay_NFC. Pure formatting,
 * no side effects on the NFC/WiFi state.
 *
 * Distributed as-is; no warranty is given.
 */
#ifndef BOMBERCAT_CORE_HEXUTILS_H
#define BOMBERCAT_CORE_HEXUTILS_H

#include <Arduino.h>

namespace HexUtils {

// Returns a space-separated hex dump such as "0x00 0x1a 0xff".
// Returns the literal "null" when len == 0, matching the legacy
// getHexRepresentation() behaviour the relay sketches relied on.
String toString(const uint8_t *data, size_t len);

// Streams the same representation as toString() without allocating a String.
// Prefer this on the hot path to keep RAM pressure low on the RP2040.
void print(Print &out, const uint8_t *data, size_t len);

}  // namespace HexUtils

#endif  // BOMBERCAT_CORE_HEXUTILS_H
