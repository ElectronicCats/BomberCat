#include "HexUtils.h"

namespace HexUtils {

static void printByte(Print &out, uint8_t b, bool leadingSpace) {
  if (leadingSpace) out.print(' ');
  out.print("0x");
  if (b <= 0x0F) out.print('0');
  out.print(b, HEX);
}

String toString(const uint8_t *data, size_t len) {
  if (len == 0) return String("null");

  String hex;
  hex.reserve(len * 5);  // "0xNN " per byte
  for (size_t i = 0; i < len; i++) {
    if (i != 0) hex += ' ';
    hex += "0x";
    if (data[i] <= 0x0F) hex += '0';
    hex += String(data[i], HEX);
  }
  return hex;
}

void print(Print &out, const uint8_t *data, size_t len) {
  if (len == 0) {
    out.print("null");
    return;
  }
  for (size_t i = 0; i < len; i++) {
    printByte(out, data[i], i != 0);
  }
}

}  // namespace HexUtils
