/**
 * BomberCat - NFCGate relay sketch
 *
 * A single, role-selectable NFCGate relay endpoint built on BomberCatCore. It
 * pairs a BomberCat with a second NFCGate peer (another BomberCat, or the
 * NFCGate Android app) through an nfcgate-server, relaying APDUs over WiFi/TCP.
 *
 *   [ physical card ] --RF--> [ BomberCat READER ] --WiFi/TCP--> nfcgate-server
 *                                                                     |
 *   [ terminal/PoS ] <--RF--- [   peer  CARD/HCE ] <--WiFi/TCP--------+
 *
 * Both roles are implemented end to end (NFCGATE_PLAN.md Fase 4 + Fase 5):
 *   - READER: receive a command over TCP -> replay to the physical card ->
 *     return the response.
 *   - CARD/HCE: read the terminal's command over RF -> forward it over TCP ->
 *     inject the peer's response back to the terminal.
 *
 * Control plane (Fase 6): the device boots into a SerialControl REPL so the
 * Python CLI in tools/ can configure it (WiFi + nfcgate params, persisted in
 * ConfigStore), start/stop the relay and read status over USB-serial — no APDUs
 * travel on the serial link. Send `ping`, `info`, `set ...`, `save`, `run`,
 * `status`, `stop`. If RELAY_AUTOSTART is set (arduino_secrets.h) the relay also
 * starts on boot from the persisted/secrets config, so it still runs standalone.
 *
 * Board: Arduino Mbed OS RP2040 (electroniccats:mbed_rp2040:bombercat).
 * Libraries: BomberCatCore, WiFiNINA, Electronic Cats PN7150.
 *
 * Distributed as-is; no warranty is given.
 */
#include <BomberCatCore.h>
#include <WiFiNINA.h>

#include "arduino_secrets.h"

#define BOMBERCAT_FW_VERSION "0.6.0"

// --- Globals (must outlive the engine / control) ---
NfcController nfc;
WiFiClient wifiClient;
NfcGateLink link(wifiClient);
ConfigStore store;
RelayConfig cfg;
RelayEngine engine(nfc, link, cfg);
SerialControl control(Serial, store, cfg, engine, BOMBERCAT_FW_VERSION);

static bool g_running = false;    // relay started (via run / autostart)?
static uint32_t g_retryAt = 0;    // non-blocking Error-retry deadline (millis)

// Build a RelayConfig from arduino_secrets.h (compile-time fallback).
static RelayConfig configFromSecrets() {
  RelayConfig c = ConfigStore::defaults();
  strncpy(c.ssid, SECRET_SSID, sizeof(c.ssid) - 1);
  strncpy(c.pass, SECRET_PASS, sizeof(c.pass) - 1);
  strncpy(c.server, RELAY_SERVER, sizeof(c.server) - 1);
  c.port = RELAY_PORT;
  c.session = RELAY_SESSION;
  c.role = RELAY_ROLE;
  return c;
}

// Associate with WiFi. Returns true once connected (or false after timeout).
static bool connectWiFi(const RelayConfig &c, uint32_t timeoutMs = 20000) {
  if (strlen(c.ssid) == 0) {
    LOG_ERROR("WiFi: empty SSID (set it via the CLI or arduino_secrets.h)");
    return false;
  }
  LOG_INFO("WiFi: connecting");
  WiFi.begin(c.ssid, c.pass);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > timeoutMs) {
      LOG_ERROR("WiFi: connect timeout");
      return false;
    }
    delay(250);
  }
  Log::out().print("WiFi: connected, IP ");
  Log::out().println(WiFi.localIP());
  return true;
}

// --- SerialControl callbacks (own the WiFi / MCU actions core/ stays free of) --
static bool runRelay() {
  if (!connectWiFi(cfg)) return false;
  if (!engine.begin()) return false;
  g_running = true;
  g_retryAt = 0;
  return true;
}

static void stopRelay() {
  engine.stop();
  g_running = false;
  g_retryAt = 0;
}

static void rebootMcu() { NVIC_SystemReset(); }

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) {
  }
  Log::begin(Serial, LogLevel::Debug);
  LOG_INFO("BomberCat NFCGate relay");

  // Load persisted config; fall back to the compile-time secrets.
  if (store.begin() && store.load(cfg)) {
    LOG_INFO("Config: loaded from flash");
  } else {
    LOG_WARN("Config: none persisted, using arduino_secrets.h");
    cfg = configFromSecrets();
  }

  SerialControl::Callbacks cb;
  cb.run = runRelay;
  cb.stop = stopRelay;
  cb.reboot = rebootMcu;
  control.setCallbacks(cb);
  control.begin();  // prints "+OK bombercat ready" so the CLI can sync

#if defined(RELAY_AUTOSTART) && RELAY_AUTOSTART
  // Standalone mode: start straight away from the loaded config. With an
  // unconfigured (empty SSID) fallback this just no-ops and waits for the CLI.
  if (!runRelay()) {
    LOG_WARN("Autostart skipped/failed; waiting for control CLI");
  }
#endif
}

void loop() {
  control.poll();  // always service the control REPL

  if (!g_running) {
    return;  // idle until `run` (or autostart) brings the relay up
  }

  engine.loop();

  // Non-blocking retry on link/handshake failure, so the control REPL stays
  // responsive between attempts (no blocking delay()).
  if (engine.state() == RelayEngine::State::Error) {
    if (g_retryAt == 0) {
      LOG_WARN("Relay in error state, retrying in 3s");
      engine.stop();
      g_retryAt = millis() + 3000;
    } else if ((int32_t)(millis() - g_retryAt) >= 0) {
      g_retryAt = 0;
      if (WiFi.status() != WL_CONNECTED) {
        connectWiFi(cfg);
      }
      engine.begin();
    }
  }
}
