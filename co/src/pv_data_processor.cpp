#include <pv_data_processor.hpp>

void PvDataProcessor::reset()
{
  accumulated = {};
  frameCount = 0;
  temperatureSampleCount = 0;
}

bool PvDataProcessor::appendFrame(const uint8_t *data, size_t length)
{
  constexpr size_t requiredLength = 31 + (DEVICE_COUNT_PER_FRAME - 1) * 40;
  if (data == nullptr || length < requiredLength
    || frameCount >= EXPECTED_FRAME_COUNT) return false;

  for (uint8_t device = 0; device < DEVICE_COUNT_PER_FRAME; device++) {
    accumulated.total_power += readUnsigned(data, device, 19, 2) / 10;
    accumulated.total_prod_today += readUnsigned(data, device, 21, 2);
    accumulated.total_prod += readUnsigned(data, device, 23, 4);
    accumulated.temperature += readUnsigned(data, device, 27, 2) / 10.0f;
    temperatureSampleCount++;
  }

  frameCount++;
  return true;
}

bool PvDataProcessor::complete(PV &result, int64_t forceThreshold) const
{
  if (frameCount != EXPECTED_FRAME_COUNT || temperatureSampleCount == 0) {
    return false;
  }

  result = accumulated;
  result.temperature /= static_cast<float>(temperatureSampleCount);
  result.pv_power = result.total_power >= forceThreshold;
  return true;
}

uint32_t PvDataProcessor::readUnsigned(const uint8_t *data,
  uint8_t deviceIndex, uint8_t startByte, uint8_t byteCount)
{
  uint32_t value = 0;
  for (uint8_t index = 0; index < byteCount; index++) {
    value = (value << 8) | data[startByte + index + deviceIndex * 40];
  }
  return value;
}
