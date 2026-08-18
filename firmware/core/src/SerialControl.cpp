/**
 * BomberCatCore - SerialControl implementation.
 *
 * Distributed as-is; no warranty is given.
 */
#include "SerialControl.h"

#include <stdlib.h>
#include <string.h>

SerialControl::SerialControl(Stream &io, ConfigStore &store, RelayConfig &cfg,
                             RelayEngine &engine, const char *fwVersion)
    : _io(io), _store(store), _cfg(cfg), _engine(engine), _fw(fwVersion) {}

void SerialControl::begin() {
  _len = 0;
  _overflow = false;
  ok("bombercat ready");
}

void SerialControl::poll() {
  while (_io.available() > 0) {
    char c = (char)_io.read();
    if (c == '\n' || c == '\r') {
      if (_overflow) {
        // The line was too long; we dropped it — report and resync.
        err("line too long");
        _overflow = false;
        _len = 0;
        continue;
      }
      if (_len == 0) {
        continue;  // ignore empty lines / bare CR-LF
      }
      _buf[_len] = '\0';
      dispatch(_buf);
      _len = 0;
      continue;
    }
    if (_overflow) {
      continue;  // still swallowing an over-long line
    }
    if (_len >= LINE_MAX - 1) {
      _overflow = true;  // wait for the newline, then error out
      continue;
    }
    _buf[_len++] = c;
  }
}

// --- Response helpers ------------------------------------------------------

void SerialControl::ok() { _io.println("+OK"); }

void SerialControl::ok(const char *msg) {
  _io.print("+OK ");
  _io.println(msg);
}

void SerialControl::err(const char *msg) {
  _io.print("-ERR ");
  _io.println(msg);
}

void SerialControl::kv(const char *key, const char *value) {
  _io.print(':');
  _io.print(key);
  _io.print(' ');
  _io.println(value);
}

void SerialControl::kv(const char *key, long value) {
  _io.print(':');
  _io.print(key);
  _io.print(' ');
  _io.println(value);
}

const char *SerialControl::roleName() const {
  return _cfg.roleEnum() == RelayRole::READER ? "reader" : "card";
}

const char *SerialControl::stateName() const {
  // The sketch owns the full state (incl. the "connecting" bring-up phases), so
  // prefer its callback; fall back to the engine's own state if none is set.
  if (_cb.state != nullptr) {
    return _cb.state();
  }
  switch (_engine.state()) {
    case RelayEngine::State::Relaying:
      return "relaying";
    case RelayEngine::State::Error:
      return "error";
    case RelayEngine::State::Idle:
    default:
      return "idle";
  }
}

// --- Dispatch --------------------------------------------------------------

// Split the leading verb off `line`; returns a pointer to the remaining args
// (leading spaces skipped), or an empty string if there are none. Mutates
// `line` in place (null-terminates the verb).
static char *splitVerb(char *line) {
  char *p = line;
  while (*p && *p != ' ') p++;
  if (*p == '\0') return p;  // no args; points at the terminator
  *p++ = '\0';
  while (*p == ' ') p++;
  return p;
}

void SerialControl::dispatch(char *line) {
  while (*line == ' ') line++;  // trim leading spaces
  char *args = splitVerb(line);
  const char *verb = line;

  if (strcmp(verb, "ping") == 0) {
    ok("bombercat");
  } else if (strcmp(verb, "info") == 0) {
    kv("fw", _fw);
    kv("role", roleName());
    kv("ssid", _cfg.ssid);
    kv("server", _cfg.server);
    kv("port", (long)_cfg.port);
    kv("session", (long)_cfg.session);
    kv("state", stateName());
    ok();
  } else if (strcmp(verb, "get") == 0) {
    if (strcmp(args, "fw") == 0) {
      kv("fw", _fw);
    } else if (strcmp(args, "role") == 0) {
      kv("role", roleName());
    } else if (strcmp(args, "ssid") == 0) {
      kv("ssid", _cfg.ssid);
    } else if (strcmp(args, "server") == 0) {
      kv("server", _cfg.server);
    } else if (strcmp(args, "port") == 0) {
      kv("port", (long)_cfg.port);
    } else if (strcmp(args, "session") == 0) {
      kv("session", (long)_cfg.session);
    } else if (strcmp(args, "state") == 0) {
      kv("state", stateName());
    } else {
      err("unknown key");
      return;
    }
    ok();
  } else if (strcmp(verb, "set") == 0) {
    if (handleSet(args)) {
      ok();
    }  // handleSet emits its own -ERR on failure
  } else if (strcmp(verb, "save") == 0) {
    _store.save(_cfg) ? ok() : err("save failed");
  } else if (strcmp(verb, "load") == 0) {
    _store.load(_cfg);  // populates defaults if none persisted
    ok();
  } else if (strcmp(verb, "clear") == 0) {
    _store.clear() ? ok() : err("clear failed");
  } else if (strcmp(verb, "run") == 0) {
    if (_cb.run == nullptr) {
      err("run unavailable");
    } else {
      // Non-blocking: run() only kicks off the bring-up. nullptr = accepted;
      // a non-null string is the reason it couldn't even start.
      const char *reason = _cb.run();
      reason == nullptr ? ok("accepted") : err(reason);
    }
  } else if (strcmp(verb, "stop") == 0) {
    if (_cb.stop != nullptr) _cb.stop();
    ok();
  } else if (strcmp(verb, "status") == 0) {
    kv("state", stateName());
    if (_cb.detail != nullptr) {
      kv("detail", _cb.detail());
    }
    kv("connected", (long)(_engine.connected() ? 1 : 0));
    kv("peer", (long)(_engine.peerReady() ? 1 : 0));
    kv("relayed", (long)_engine.relayedCount());
    ok();
  } else if (strcmp(verb, "identify") == 0) {
    if (_cb.identify == nullptr) {
      err("identify unavailable");
    } else {
      _cb.identify();  // only ARMS the blink; the sketch's loop() runs it
      ok();
    }
  } else if (strcmp(verb, "capture") == 0) {
    // Arm/disarm the APDU capture tap (Fase 8). When on, the engine copies each
    // relayed APDU out as a ":apdu <dir> <ts_ms> <hex>" event on this same
    // stream, which the host `bombercat capture` turns into a pcap for
    // Wireshark. Off the relay hot path — only a copy of the log we already emit.
    if (strcmp(args, "on") == 0) {
      _engine.setCapture(&_io);
      ok("capture on");
    } else if (strcmp(args, "off") == 0) {
      _engine.setCapture(nullptr);
      ok("capture off");
    } else if (args[0] == '\0' || strcmp(args, "status") == 0) {
      kv("capture", (long)(_engine.capturing() ? 1 : 0));
      ok();
    } else {
      err("capture must be on|off");
    }
  } else if (strcmp(verb, "reboot") == 0) {
    ok();
    _io.flush();
    if (_cb.reboot != nullptr) _cb.reboot();
  } else {
    err("unknown command");
  }
}

bool SerialControl::handleSet(char *args) {
  char *value = splitVerb(args);  // args := key, value := rest of line
  const char *key = args;

  if (strcmp(key, "ssid") == 0) {
    strncpy(_cfg.ssid, value, sizeof(_cfg.ssid) - 1);
    _cfg.ssid[sizeof(_cfg.ssid) - 1] = '\0';
  } else if (strcmp(key, "pass") == 0) {
    strncpy(_cfg.pass, value, sizeof(_cfg.pass) - 1);
    _cfg.pass[sizeof(_cfg.pass) - 1] = '\0';
  } else if (strcmp(key, "server") == 0) {
    strncpy(_cfg.server, value, sizeof(_cfg.server) - 1);
    _cfg.server[sizeof(_cfg.server) - 1] = '\0';
  } else if (strcmp(key, "port") == 0) {
    char *end = nullptr;
    long p = strtol(value, &end, 10);
    if (end == value || *end != '\0' || p < 1 || p > 65535) {
      err("port must be 1..65535");
      return false;
    }
    _cfg.port = (uint16_t)p;
  } else if (strcmp(key, "session") == 0) {
    char *end = nullptr;
    long s = strtol(value, &end, 10);
    if (end == value || *end != '\0' || s < 1 || s > 255) {
      err("session must be 1..255");
      return false;
    }
    _cfg.session = (uint8_t)s;
  } else if (strcmp(key, "role") == 0) {
    if (strcmp(value, "reader") == 0) {
      _cfg.setRole(RelayRole::READER);
    } else if (strcmp(value, "card") == 0) {
      _cfg.setRole(RelayRole::CARD);
    } else {
      err("role must be reader|card");
      return false;
    }
  } else {
    err("unknown key");
    return false;
  }
  return true;
}
