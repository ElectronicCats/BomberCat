/**
 * BomberCatCore - FlashIAPLimits
 *
 * Computes the usable region of the RP2040 internal flash that sits after the
 * sketch image, for use as a TDBStore-backed key/value area. Vendored verbatim
 * from the relay sketches so host/client can migrate onto core and drop their
 * per-sketch copies without behaviour change.
 *
 * mbed_rp2040 core only.
 *
 * Distributed as-is; no warranty is given.
 */
#ifndef BOMBERCAT_CORE_FLASHIAPLIMITS_H
#define BOMBERCAT_CORE_FLASHIAPLIMITS_H

#include <Arduino.h>
#include <FlashIAP.h>
#include <FlashIAPBlockDevice.h>

namespace mbed {

// Usable FlashIAP block-device region past the firmware image.
struct FlashIAPLimits {
  size_t flash_size;
  uint32_t start_address;
  uint32_t available_size;
};

inline FlashIAPLimits getFlashIAPLimits() {
  auto align_down = [](uint64_t val, uint64_t size) {
    return (((val) / size)) * size;
  };
  auto align_up = [](uint32_t val, uint32_t size) {
    return (((val - 1) / size) + 1) * size;
  };

  size_t flash_size;
  uint32_t flash_start_address;
  uint32_t start_address;
  FlashIAP flash;

  auto result = flash.init();
  if (result != 0) return {};

  // Find the start of the first sector after the text area.
  int sector_size = flash.get_sector_size(FLASHIAP_APP_ROM_END_ADDR);
  start_address = align_up(FLASHIAP_APP_ROM_END_ADDR, sector_size);
  flash_start_address = flash.get_flash_start();
  flash_size = flash.get_flash_size();

  result = flash.deinit();

  int available_size = flash_start_address + flash_size - start_address;
  if (available_size % (sector_size * 2)) {
    available_size = align_down(available_size, sector_size * 2);
  }

  return {flash_size, start_address, available_size};
}

}  // namespace mbed

#endif  // BOMBERCAT_CORE_FLASHIAPLIMITS_H
