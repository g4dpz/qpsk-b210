#include "dsp/crc.hpp"

namespace mpsk::dsp {

uint16_t crc16_ccitt(const uint8_t* data, std::size_t size) {
  uint16_t crc = 0xFFFF;
  for (std::size_t i = 0; i < size; ++i) {
    // Bring the next byte into the top of the register, then divide by the
    // polynomial one bit at a time: shift left, and XOR in 0x1021 whenever a
    // 1 falls off the top.
    crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(data[i]) << 8));
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

uint32_t crc32_ieee(const uint8_t* data, std::size_t size) {
  uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < size; ++i) {
    // Reflected form: bits are processed least significant first, so the
    // register shifts right and the polynomial is the bit-reversed 0xEDB88320.
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
  }
  return ~crc;
}

}  // namespace mpsk::dsp
