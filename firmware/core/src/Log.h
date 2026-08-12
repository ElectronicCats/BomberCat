/**
 * BomberCatCore - Log
 *
 * Minimal, level-gated logging over an Arduino Stream. Replaces the pervasive
 * `if (debug) { Serial.println(...); }` blocks in the relay sketches with a
 * single runtime-adjustable level, so debug output can be toggled from the
 * control CLI without recompiling.
 *
 * The control-protocol responses the CLI parses ("OK" / "ERROR") are NOT log
 * lines: keep printing those directly to the control stream so they are never
 * suppressed by the log level.
 *
 * Distributed as-is; no warranty is given.
 */
#ifndef BOMBERCAT_CORE_LOG_H
#define BOMBERCAT_CORE_LOG_H

#include <Arduino.h>

#include "HexUtils.h"

// PascalCase members are deliberate: the PN7150 library #defines ERROR/SUCCESS
// (and other libs #define DEBUG), and the C preprocessor would rewrite an
// all-caps `LogLevel::ERROR` into `LogLevel::1`. Mixed-case names dodge every
// such macro.
enum class LogLevel : uint8_t {
  None = 0,
  Error = 1,
  Warn = 2,
  Info = 3,
  Debug = 4,
};

class Log {
 public:
  // Bind the sink stream and initial level. Until begin() is called, all
  // logging is silently dropped (out() returns a null sink).
  static void begin(Stream &out, LogLevel level = LogLevel::Info);

  static void setLevel(LogLevel level) { _level = level; }
  static LogLevel getLevel() { return _level; }

  // True when a message of `level` would be emitted at the current setting.
  static bool enabled(LogLevel level) {
    return _out != nullptr && level != LogLevel::None &&
           static_cast<uint8_t>(level) <= static_cast<uint8_t>(_level);
  }

  // Raw sink for custom formatting; always non-null after begin() so callers
  // can `Log::out().print(...)`. Guard with enabled() to respect the level.
  static Print &out();

  // Emit `msg` as a single line if `level` is enabled.
  static void line(LogLevel level, const char *msg);
  static void line(LogLevel level, const String &msg);

  // Emit `label` followed by a hex dump of `data` if `level` is enabled.
  static void hex(LogLevel level, const char *label, const uint8_t *data,
                  size_t len);

 private:
  static Stream *_out;
  static LogLevel _level;
};

// Convenience wrappers; the arguments are still evaluated, so avoid heavy
// String building on hot paths when the level is likely disabled.
#define LOG_ERROR(msg) Log::line(LogLevel::Error, (msg))
#define LOG_WARN(msg) Log::line(LogLevel::Warn, (msg))
#define LOG_INFO(msg) Log::line(LogLevel::Info, (msg))
#define LOG_DEBUG(msg) Log::line(LogLevel::Debug, (msg))

#endif  // BOMBERCAT_CORE_LOG_H
