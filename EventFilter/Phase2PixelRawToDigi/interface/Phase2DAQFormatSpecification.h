#ifndef Phase2DAQFormatSpecification_H
#define Phase2DAQFormatSpecification_H

namespace Phase2DAQFormatSpecification {

  static constexpr int SLINKS_PER_DTC = 16;
  static constexpr uint16_t CHIP_HEADER_MARKER = 0xE000;
  static constexpr uint32_t HEADER_TRAILER_PATTERN = 0xFFFFFFFF;
  static constexpr int HEADER_TRAILER_LINES = 4;
  static constexpr int BITS_PER_WORD = 32;
  static constexpr int BITS_PER_CHUNK = 128;
  static constexpr int BYTES_PER_WORD = 2;

  static constexpr int HITMAP_SIZE = 16;
  static constexpr int HITMAP_ROW = 4;
  static constexpr int HITMAP_COL = 4;

};  // namespace Phase2DAQFormatSpecification

#endif
