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
 * `status`, `stop`, `identify`. If RELAY_AUTOSTART is set (arduino_secrets.h) the
 * relay also starts on boot from the persisted/secrets config, so it still runs
 * standalone.
 *
 * With several BomberCats attached the CLI addresses each one by a stable ID
 * derived from its USB identity (`bombercat device list`, then `-d <id>` on any
 * command); `identify` blinks the LED so an ID can be matched to a board.
 *
 * Board: Arduino Mbed OS RP2040 (electroniccats:mbed_rp2040:bombercat).
 * Libraries: BomberCatCore, WiFiNINA, Electronic Cats PN7150.
 *
 * Distributed as-is; no warranty is given.
 */
#include <BomberCatCore.h>
#include <WiFiNINA.h>

#include "arduino_secrets.h"

#define BOMBERCAT_FW_VERSION "0.9.2"

// Bound the server TCP connect so a host that is reachable but silently drops
// the SYN fails fast (clean -ERR) instead of hanging tens of seconds and blowing
// past the CLI's run window. WiFiNINA's default connTimeout (0) leaves it to the
// NINA firmware's long default. Keep the worst-case budget aligned with the CLI:
//   CLI run read_timeout >= WiFi(20s) + NFC + TCP_CONNECT + SYN + margin.
// See tools/modules/core/bombercat.py run() and firmware/DEBUG_run_timeout_mismatch.md.
#define RELAY_TCP_CONNECT_TIMEOUT_MS 8000

// --- Globals (must outlive the engine / control) ---
NfcController nfc;
WiFiClient wifiClient;
NfcGateLink link(wifiClient);
ConfigStore store;
RelayConfig cfg;
RelayEngine engine(nfc, link, cfg);
SerialControl control(Serial, store, cfg, engine, BOMBERCAT_FW_VERSION);

// --- Non-blocking `run` bring-up state machine ---------------------------------
// `run` is intentionally NOT blocking: it only kicks the bring-up off (returns
// `+OK accepted` at once) and loop() advances it one phase per iteration, so the
// serial REPL is serviced throughout and a slow/stuck phase can never wedge the
// device. Progress is published over `status` (idle -> connecting -> relaying |
// error). This replaces the old synchronous runRelay() that blocked loop() for
// the whole WiFi+NFC+TCP+SYN sequence — see firmware/DEBUG_run_timeout_mismatch.md.
enum class Phase : uint8_t {
  Idle,       // nothing running; waiting for `run`
  Wifi,       // associating WiFi (non-blocking poll, hard deadline)
  Nfc,        // bringing up the PN7150 in the configured role
  Tcp,        // opening the TCP link to nfcgate-server (bounded connect)
  Syn,        // registering with the session (OP_SYN)
  Relaying,   // fully up; engine.loop() shuttles APDUs
  Error,      // a bring-up step failed, or the link dropped while relaying
};

static Phase g_phase = Phase::Idle;
static uint32_t g_wifiDeadline = 0;   // WiFi associate hard timeout (millis)
static bool g_wifiBegun = false;      // WiFi.begin() issued for this attempt?
static uint32_t g_retryAt = 0;        // >0 only after a runtime link loss: when
                                      // to auto-retry. A bring-up failure leaves
                                      // it 0 (stay in Error until the user acts).
static const char *g_detail = "";     // human-readable phase / last-error text

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

// Kick off the WiFi association for a fresh attempt: reset the phase flags and
// enter Phase::Wifi. driveBringup() (in loop()) does the actual non-blocking
// polling from here on. Static WiFi.begin() itself is issued lazily on the first
// Wifi tick so this returns immediately.
static void startWifi() {
  g_wifiBegun = false;
  g_wifiDeadline = 0;
  g_detail = "associating WiFi";
  g_phase = Phase::Wifi;
}

// Advance the bring-up by at most one phase. Called every loop() iteration; a
// no-op unless a bring-up is in progress. Each phase either progresses, or fails
// into Phase::Error with g_detail set. Only the WiFi phase is spread across
// iterations (its 20 s deadline is the long one); NFC and TCP are single bounded
// calls, so they occupy one iteration each but never hang the REPL for long.
static void driveBringup() {
  switch (g_phase) {
    case Phase::Wifi:
      if (!g_wifiBegun) {
        LOG_INFO("WiFi: connecting");
        WiFi.begin(cfg.ssid, cfg.pass);
        g_wifiBegun = true;
        g_wifiDeadline = millis() + 20000;
      }
      if (WiFi.status() == WL_CONNECTED) {
        Log::out().print("WiFi: connected, IP ");
        Log::out().println(WiFi.localIP());
        g_detail = "bringing up NFC";
        g_phase = Phase::Nfc;
      } else if ((int32_t)(millis() - g_wifiDeadline) >= 0) {
        LOG_ERROR("WiFi: connect timeout");
        g_detail = "WiFi connect timeout";
        g_phase = Phase::Error;
      }
      break;

    case Phase::Nfc:
      if (engine.beginNfc()) {
        g_detail = "connecting server";
        g_phase = Phase::Tcp;
      } else {
        g_detail = "NFC bring-up failed (PN7150)";
        g_phase = Phase::Error;
      }
      break;

    case Phase::Tcp:
      if (engine.connectLink()) {
        g_detail = "registering session";
        g_phase = Phase::Syn;
      } else {
        g_detail = "server connect failed";
        g_phase = Phase::Error;
      }
      break;

    case Phase::Syn:
      if (engine.announce()) {
        g_detail = "relaying";
        g_phase = Phase::Relaying;
      } else {
        g_detail = "session register failed";
        g_phase = Phase::Error;
      }
      break;

    default:
      break;  // Idle / Relaying / Error: nothing to advance here
  }
}

// --- SerialControl callbacks (own the WiFi / MCU actions core/ stays free of) --

// `run`: kick off the bring-up. Returns nullptr = accepted (bring-up started),
// or a short reason it can't start (reported as -ERR). Never blocks on WiFi/NFC.
static const char *runRelay() {
  switch (g_phase) {
    case Phase::Relaying:
      return "already running";
    case Phase::Wifi:
    case Phase::Nfc:
    case Phase::Tcp:
    case Phase::Syn:
      return "already starting";
    default:
      break;  // Idle / Error: OK to (re)start
  }
  if (strlen(cfg.ssid) == 0) {
    return "empty SSID (set it via the CLI or arduino_secrets.h)";
  }
  g_retryAt = 0;
  startWifi();
  return nullptr;  // accepted; loop() takes it from here
}

static void stopRelay() {
  engine.stop();
  g_phase = Phase::Idle;
  g_wifiBegun = false;
  g_retryAt = 0;
  g_detail = "";
}

static void rebootMcu() { NVIC_SystemReset(); }

// --- `identify`: blink the LED so a CLI device ID maps to a board on the desk --
// With several BomberCats attached the CLI addresses them by number
// (`bombercat device list` / `-d <id>`); those numbers come from USB identity, so
// this is what ties one to the physical board. Armed by the `identify` command
// and driven from loop() — never blocking, so it is safe to ask a relaying
// device to identify itself mid-session.
static const uint32_t IDENTIFY_DURATION_MS = 2000;
static const uint32_t IDENTIFY_PERIOD_MS = 150;  // half-period: on/off toggle

static uint32_t g_identifyUntil = 0;  // 0 = not identifying
static uint32_t g_identifyNext = 0;   // next LED toggle (millis)
static bool g_identifyLedOn = false;

static void startIdentify() {
  pinMode(LED_BUILTIN, OUTPUT);
  g_identifyUntil = millis() + IDENTIFY_DURATION_MS;
  if (g_identifyUntil == 0) g_identifyUntil = 1;  // 0 is the "idle" sentinel
  g_identifyNext = millis();  // toggle on the next loop() iteration
}

static void driveIdentify() {
  if (g_identifyUntil == 0) return;
  if ((int32_t)(millis() - g_identifyUntil) >= 0) {  // done: leave the LED off
    g_identifyUntil = 0;
    g_identifyLedOn = false;
    digitalWrite(LED_BUILTIN, LOW);
    return;
  }
  if ((int32_t)(millis() - g_identifyNext) >= 0) {
    g_identifyLedOn = !g_identifyLedOn;
    digitalWrite(LED_BUILTIN, g_identifyLedOn ? HIGH : LOW);
    g_identifyNext = millis() + IDENTIFY_PERIOD_MS;
  }
}

// Control-plane state name for `status`/`info` (see SerialControl::Callbacks).
static const char *relayStateName() {
  switch (g_phase) {
    case Phase::Wifi:
    case Phase::Nfc:
    case Phase::Tcp:
    case Phase::Syn:
      return "connecting";
    case Phase::Relaying:
      return "relaying";
    case Phase::Error:
      return "error";
    case Phase::Idle:
    default:
      return "idle";
  }
}

static const char *relayDetail() { return g_detail; }

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) {
  }
  // Relay runs SILENT by default (Warn): the per-APDU Debug hex dumps used to
  // run on every relayed APDU and, standalone (no host draining USB CDC), could
  // block Serial.print mid-transaction on the relay hot path — added latency for
  // no benefit once the relay is validated. Raise it at runtime with the CLI
  // (`loglevel 4`) or `bombercat monitor` when diagnosing. Capture is a separate
  // sink (`capture on`) and is unaffected by this level.
  Log::begin(Serial, LogLevel::Warn);
  LOG_WARN("BomberCat NFCGate relay");

  // Cap the server connect so the Tcp phase (engine.connectLink()) can't stall
  // on an unresponsive host (the NfcGateLink only sees a Client&, so the bound
  // is set here on the concrete WiFiClient it wraps).
  wifiClient.setConnectionTimeout(RELAY_TCP_CONNECT_TIMEOUT_MS);

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
  cb.identify = startIdentify;
  cb.state = relayStateName;
  cb.detail = relayDetail;
  control.setCallbacks(cb);
  control.begin();  // prints "+OK bombercat ready" so the CLI can sync

#if defined(RELAY_AUTOSTART) && RELAY_AUTOSTART
  // Standalone mode: kick off the bring-up straight from the loaded config. It
  // runs non-blocking in loop(), so a missing server/PN7150 can't stall boot or
  // the REPL. With an unconfigured (empty SSID) fallback this just no-ops.
  if (runRelay() != nullptr) {
    LOG_WARN("Autostart not started (unconfigured?); waiting for control CLI");
  }
#endif
}

void loop() {
  control.poll();  // always service the control REPL, every iteration

  driveIdentify();  // run an armed `identify` blink (no-op when not identifying)

  driveBringup();  // advance any in-progress bring-up (no-op when not connecting)

  if (g_phase == Phase::Relaying) {
    engine.loop();
    // Link dropped mid-relay: schedule a non-blocking auto-retry (g_retryAt > 0
    // marks this as a *runtime* loss, which we recover from — unlike a bring-up
    // failure, which stays in Error until the user re-runs).
    if (engine.state() == RelayEngine::State::Error) {
      LOG_WARN("Relay link lost, retrying in 3s");
      engine.stop();
      g_detail = "link lost, retrying";
      g_retryAt = millis() + 3000;
      g_phase = Phase::Error;
    }
  } else if (g_phase == Phase::Error && g_retryAt != 0 &&
             (int32_t)(millis() - g_retryAt) >= 0) {
    // Retry after a runtime link loss: re-associate WiFi if it dropped, else go
    // straight back to reconnecting the TCP link (the PN7150 is still up).
    g_retryAt = 0;
    g_wifiBegun = false;
    if (WiFi.status() == WL_CONNECTED) {
      g_detail = "reconnecting server";
      g_phase = Phase::Tcp;
    } else {
      startWifi();
    }
  }
}
