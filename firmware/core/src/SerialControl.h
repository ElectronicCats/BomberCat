/**
 * BomberCatCore - SerialControl
 *
 * A tiny line-based control REPL over a Stream (USB-serial), so the Python
 * control CLI in tools/ can configure, start/stop and monitor the relay without
 * carrying any APDUs on the wire (APDUs go over WiFi/TCP — see docs/NFCGATE_PLAN.md).
 *
 * This is the device end of docs/NFCGATE_PLAN.md Fase 6.
 *
 * Wire protocol (ASCII, one command per line, '\n'-terminated):
 *
 *   CLI -> device :  <cmd> [args...]\n
 *   device -> CLI :  one or more RESPONSE lines, each with a leading marker so
 *                    the CLI can tell them apart from human log output:
 *                      ":<key> <value>"  a datum (info/status fields)
 *                      "+OK [text]"       command succeeded (terminates a reply)
 *                      "-ERR <text>"      command failed  (terminates a reply)
 *                    Any line NOT starting with ':' '+' or '-' is log noise and
 *                    the CLI ignores it. Every command yields exactly one
 *                    terminating +OK / -ERR line.
 *
 * Commands:
 *   ping                      -> +OK bombercat            (handshake / discovery)
 *   info                      -> :fw :role :ssid :server :port :session :state
 *                                +OK
 *   get <key>                 -> :<key> <value> +OK       (key as in `set`)
 *   set <key> <value...>      -> +OK / -ERR   keys: ssid pass server port
 *                                session role  (value = rest of line; role is
 *                                reader|card, port/session numeric)
 *   save                      -> +OK / -ERR   persist current config to flash
 *   load                      -> +OK          reload config from flash
 *   clear                     -> +OK / -ERR   erase persisted config
 *   run                       -> +OK accepted / -ERR <reason>
 *                                Non-blocking: only KICKS OFF the bring-up and
 *                                replies immediately. The relay then advances
 *                                through WiFi -> NFC -> TCP -> SYN in the
 *                                sketch's loop(); poll `status` for progress
 *                                (state goes idle -> connecting -> relaying|error).
 *   stop                      -> +OK          stop the relay
 *   status                    -> :state :detail :connected :peer :relayed +OK
 *                                state: idle|connecting|relaying|error;
 *                                detail: current phase or last error (human text)
 *   identify                  -> +OK           blink the LED for a couple of
 *                                seconds so the user can tell which physical
 *                                board a CLI device ID refers to. Returns at
 *                                once; the blinking runs from the sketch loop().
 *   capture <on|off>          -> +OK capture on|off   arm/disarm the APDU tap.
 *                                While on, the engine copies each relayed APDU
 *                                out as ":apdu <dir> <ts_ms> <hex>" (dir=cmd|resp)
 *                                on this stream — the host turns it into a pcap
 *                                for Wireshark (Fase 8). Bare `capture` or
 *                                `capture status` -> :capture <0|1> +OK.
 *   reboot                    -> +OK, then resets the MCU
 *
 * The relay actions (run/stop/reboot) are provided by the sketch as callbacks so
 * this class — and all of core/ — stays free of any WiFiNINA dependency (the
 * same decoupling as NfcGateLink / RelayEngine).
 *
 * Distributed as-is; no warranty is given.
 */
#ifndef BOMBERCAT_CORE_SERIALCONTROL_H
#define BOMBERCAT_CORE_SERIALCONTROL_H

#include <Arduino.h>

#include "ConfigStore.h"
#include "RelayEngine.h"

class SerialControl {
 public:
  // Actions/queries that need hardware or state the sketch owns (WiFi
  // association, the bring-up phase machine, MCU reset). All optional; a null
  // callback makes the matching command return -ERR (or fall back for state).
  struct Callbacks {
    // Kick off the (non-blocking) relay bring-up. Returns nullptr when accepted,
    // or a short human error string (reported as -ERR) when it can't start
    // (e.g. empty SSID, already running). Must return promptly — it only starts
    // the bring-up; the sketch's loop() advances it and `status` reports it.
    const char *(*run)() = nullptr;
    void (*stop)() = nullptr;   // stop relay (engine.stop + drop WiFi if wanted)
    void (*reboot)() = nullptr;  // reset the MCU
    // Start a short visual identification (LED blink) so the user can match a
    // CLI device ID to a board on the desk. Must return promptly — the sketch
    // drives the blinking from loop(), it must not stall the relay.
    void (*identify)() = nullptr;
    // Control-plane state name: "idle"|"connecting"|"relaying"|"error". Owned by
    // the sketch because the "connecting" sub-phases (WiFi/NFC/TCP/SYN) live
    // there. If null, `status`/`info` fall back to the engine's own state.
    const char *(*state)() = nullptr;
    // Optional human-readable detail (current phase or last error). If null, the
    // `status` reply omits the :detail line.
    const char *(*detail)() = nullptr;
  };

  // `io`, `store`, `cfg` and `engine` must outlive the control object (all are
  // typically globals in the sketch). `fwVersion` is reported by `info`/`get fw`.
  SerialControl(Stream &io, ConfigStore &store, RelayConfig &cfg,
                RelayEngine &engine, const char *fwVersion);

  void setCallbacks(const Callbacks &cb) { _cb = cb; }

  // Announce readiness so the CLI can sync (prints "+OK bombercat ready").
  void begin();

  // Read whatever bytes are available and dispatch each complete line. Call once
  // per loop(); never blocks.
  void poll();

 private:
  void dispatch(char *line);

  // Response helpers (the leading-marker protocol above).
  void ok();
  void ok(const char *msg);
  void err(const char *msg);
  void kv(const char *key, const char *value);
  void kv(const char *key, long value);

  bool handleSet(char *args);  // `set <key> <value...>`; returns false -> -ERR
  const char *roleName() const;
  const char *stateName() const;

  Stream &_io;
  ConfigStore &_store;
  RelayConfig &_cfg;
  RelayEngine &_engine;
  const char *_fw;
  Callbacks _cb;

  static const size_t LINE_MAX = 160;  // ssid/pass up to 63 each + verb + space
  char _buf[LINE_MAX];
  size_t _len = 0;
  bool _overflow = false;  // current line exceeded LINE_MAX; drop to next '\n'
};

#endif  // BOMBERCAT_CORE_SERIALCONTROL_H
