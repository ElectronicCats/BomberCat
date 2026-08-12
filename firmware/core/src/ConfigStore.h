/**
 * BomberCatCore - ConfigStore
 *
 * Persistent configuration for the NFCGate relay, backed by mbed TDBStore on a
 * FlashIAP block device. Refactor of the duplicated SketchStats get/set +
 * FlashIAP setup blocks in host_Relay_NFC / client_Relay_NFC, re-scoped from
 * the old MQTT parameters to the NFCGate ones (server host/port, session byte,
 * role) per NFCGATE_PLAN.md and firmware/core/proto/UPSTREAM.md.
 *
 * mbed_rp2040 core only (TDBStore / FlashIAPBlockDevice).
 *
 * Distributed as-is; no warranty is given.
 */
#ifndef BOMBERCAT_CORE_CONFIGSTORE_H
#define BOMBERCAT_CORE_CONFIGSTORE_H

#include <Arduino.h>

// FlashIAPBlockDevice lives in the global namespace; TDBStore in mbed.
class FlashIAPBlockDevice;
namespace mbed {
class TDBStore;
}  // namespace mbed

// Which end of the relay this device plays. Matches NFCData.data_source roles
// documented in proto/UPSTREAM.md: READER reads a physical card, CARD emulates
// one to a terminal (HCE).
enum class RelayRole : uint8_t {
  READER = 0,
  CARD = 1,
};

// Default nfcgate-server port at the pinned upstream (server@4d32cc1).
static const uint16_t NFCGATE_DEFAULT_PORT = 5566;

// Fixed-size, flash-persistable configuration record. Kept as a POD so it can
// be written/read to TDBStore as a raw byte blob. Do not add non-trivial types.
struct RelayConfig {
  char ssid[64];    // WiFi SSID
  char pass[64];    // WiFi passphrase
  char server[64];  // nfcgate-server host (name or IP)
  uint16_t port;    // nfcgate-server TCP port
  uint8_t session;  // session byte (1..255); groups the two relay peers
  uint8_t role;     // RelayRole as raw byte

  RelayRole roleEnum() const { return static_cast<RelayRole>(role); }
  void setRole(RelayRole r) { role = static_cast<uint8_t>(r); }
};

class ConfigStore {
 public:
  ConfigStore() = default;
  ~ConfigStore();

  // Initialise the FlashIAP block device and TDBStore. Returns true on success.
  // Safe to call more than once (subsequent calls are no-ops returning true).
  bool begin();

  // Load persisted config into `out`. Returns true if a saved record was found.
  // When false, `out` is populated with defaults() so callers can always use it.
  bool load(RelayConfig &out);

  // Persist `cfg`. Returns true on success.
  bool save(const RelayConfig &cfg);

  // Erase the persisted record. Returns true on success (or if none existed).
  bool clear();

  // Factory defaults: empty credentials, default port, session 1, READER role.
  static RelayConfig defaults();

 private:
  bool _ready = false;
  FlashIAPBlockDevice *_bd = nullptr;
  mbed::TDBStore *_store = nullptr;
};

#endif  // BOMBERCAT_CORE_CONFIGSTORE_H
