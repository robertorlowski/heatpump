#include <pv_data_processor.hpp>

void PvDataProcessor::reset()
{
  accumulated = {};
  frameCount = 0;
  temperatureSampleCount = 0;
}

bool PvDataProcessor::appendFrame(const uint8_t *data, size_t length)
{
  // Modbus response header: address, function code, byte count.
  constexpr size_t HEADER_LENGTH = 3;
  if (data == nullptr || length <= HEADER_LENGTH
    || frameCount >= EXPECTED_FRAME_COUNT) return false;

  // The record count is taken from the response itself, so adding or removing
  // a panel does not require a matching constant in the firmware.
  const size_t payloadLength = data[2];
  if (payloadLength == 0 || payloadLength % PV_BYTES_PER_DEVICE != 0
    || length < HEADER_LENGTH + payloadLength) return false;
  const uint8_t deviceCount = payloadLength / PV_BYTES_PER_DEVICE;

  // Offsets are absolute: Modbus header plus the offset inside the 40-byte
  // Hoymiles port record (power 16, today 18, total 20, temperature 24).
  for (uint8_t device = 0; device < deviceCount; device++) {
    accumulated.total_power += readUnsigned(data, device, 19, 2) / 10;
    accumulated.total_prod_today += readUnsigned(data, device, 21, 2);
    accumulated.total_prod += readUnsigned(data, device, 23, 4);
    // Inverter temperature is a signed value in 0.1 degrees Celsius.
    accumulated.temperature +=
      static_cast<int16_t>(readUnsigned(data, device, 27, 2)) / 10.0f;
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
    const size_t offset =
      startByte + index + deviceIndex * PV_BYTES_PER_DEVICE;
    value = (value << 8) | data[offset];
  }
  return value;
}
