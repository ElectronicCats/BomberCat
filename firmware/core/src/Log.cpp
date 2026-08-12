#include "Log.h"

// A Print sink that discards everything, used before begin() so out() is always
// safe to dereference.
namespace {
class NullPrint : public Print {
 public:
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t *, size_t size) override { return size; }
};
NullPrint g_nullSink;
}  // namespace

Stream *Log::_out = nullptr;
LogLevel Log::_level = LogLevel::Info;

void Log::begin(Stream &out, LogLevel level) {
  _out = &out;
  _level = level;
}

Print &Log::out() {
  if (_out != nullptr) return *_out;
  return g_nullSink;
}

void Log::line(LogLevel level, const char *msg) {
  if (!enabled(level)) return;
  _out->println(msg);
}

void Log::line(LogLevel level, const String &msg) {
  if (!enabled(level)) return;
  _out->println(msg);
}

void Log::hex(LogLevel level, const char *label, const uint8_t *data,
              size_t len) {
  if (!enabled(level)) return;
  if (label != nullptr) {
    _out->print(label);
    _out->print(' ');
  }
  HexUtils::print(*_out, data, len);
  _out->println();
}
