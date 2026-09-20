#pragma once

#include <cstddef>
#include <cstdint>

#include <domain_types.hpp>

class PvDataProcessor {
public:
  void reset();
  bool appendFrame(const uint8_t *data, size_t length);
  bool complete(PV &result, int64_t forceThreshold) const;

private:
  static constexpr uint8_t DEVICE_COUNT_PER_FRAME = 5;
  static constexpr uint8_t EXPECTED_FRAME_COUNT = 2;

  PV accumulated{};
  uint8_t frameCount = 0;
  uint8_t temperatureSampleCount = 0;

  static uint32_t readUnsigned(const uint8_t *data, uint8_t deviceIndex,
    uint8_t startByte, uint8_t byteCount);
};
