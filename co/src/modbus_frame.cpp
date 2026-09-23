#include <modbus_frame.hpp>

#include <cmath>

#include <hardware_config.hpp>

namespace {
constexpr uint8_t HP_DEVICE_ID = 0x41;
constexpr size_t HP_FRAME_LENGTH = 5;
constexpr size_t PV_FRAME_LENGTH = 8;

uint8_t high(uint16_t value)
{
  return static_cast<uint8_t>(value >> 8);
}

uint8_t low(uint16_t value)
{
  return static_cast<uint8_t>(value & 0xFF);
}

// Pump setpoints travel as whole units in one byte and hundredths in the next.
void writeScaled(uint8_t *buffer, double value, double maximum, double scale)
{
  const double bounded = value < 0 ? 0 : value > maximum ? maximum : value;
  const uint16_t scaled = static_cast<uint16_t>(std::round(bounded * scale));
  buffer[2] = static_cast<uint8_t>(scaled / 100);
  buffer[3] = static_cast<uint8_t>(scaled % 100);
}

size_t encodeHpCommand(uint8_t function, uint8_t payload, uint8_t *buffer)
{
  buffer[0] = HP_DEVICE_ID;
  buffer[1] = function;
  buffer[2] = payload;
  buffer[4] = 0xFF;
  return HP_FRAME_LENGTH;
}

size_t encodePvRead(SERIAL_OPERATION operation, uint8_t *buffer)
{
  constexpr uint16_t blockRegisters =
    PV_DEVICES_PER_REQUEST * PV_REGISTERS_PER_DEVICE;
  const uint16_t start = operation == GET_PV_DATA_1
    ? PV_FIRST_REGISTER : PV_FIRST_REGISTER + blockRegisters;

  buffer[0] = PV_DEVICE_ID;
  buffer[1] = 0x03;
  buffer[2] = high(start);
  buffer[3] = low(start);
  buffer[4] = high(blockRegisters);
  buffer[5] = low(blockRegisters);

  const uint16_t crc = modbusCrc(buffer, 6);
  // Modbus RTU transmits the CRC low byte first.
  buffer[6] = low(crc);
  buffer[7] = high(crc);
  return PV_FRAME_LENGTH;
}
}

uint16_t modbusCrc(const uint8_t *data, size_t length)
{
  uint16_t crc = 0xFFFF;
  for (size_t index = 0; index < length; index++) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001)
        : static_cast<uint16_t>(crc >> 1);
    }
  }
  return crc;
}

size_t encodeCommand(SERIAL_OPERATION operation, double value,
  uint8_t *buffer, size_t capacity)
{
  if (buffer == nullptr || capacity < MODBUS_FRAME_CAPACITY) return 0;
  for (size_t index = 0; index < MODBUS_FRAME_CAPACITY; index++) {
    buffer[index] = 0;
  }

  switch (operation) {
    case GET_HP_DATA:
      return encodeHpCommand(0x01, 0, buffer);

    case GET_PV_DATA_1:
    case GET_PV_DATA_2:
      return encodePvRead(operation, buffer);

    case SET_HP_FORCE_ON:
    case SET_HP_FORCE_OFF:
      return encodeHpCommand(0x03, operation == SET_HP_FORCE_ON, buffer);

    case SET_HP_CO_ON:
    case SET_HP_CO_OFF:
      return encodeHpCommand(0x0C, operation == SET_HP_CO_ON, buffer);

    case SET_SUMP_HEATER_ON:
    case SET_SUMP_HEATER_OFF:
      return encodeHpCommand(0x0B, operation == SET_SUMP_HEATER_ON, buffer);

    case SET_COLD_PUMP_ON:
    case SET_COLD_PUMP_OFF:
      return encodeHpCommand(0x0A, operation == SET_COLD_PUMP_ON, buffer);

    case SET_HOT_PUMP_ON:
    case SET_HOT_PUMP_OFF:
      return encodeHpCommand(0x09, operation == SET_HOT_PUMP_ON, buffer);

    case SET_T_SETPOINT_CO:
    case SET_T_DELTA_CO:
    case SET_EEV_SETPOINT: {
      buffer[0] = HP_DEVICE_ID;
      buffer[1] = operation == SET_T_SETPOINT_CO ? 0x04
        : operation == SET_T_DELTA_CO ? 0x05 : 0x08;
      writeScaled(buffer, value, 255.99, 100.0);
      buffer[4] = 0xFF;
      return HP_FRAME_LENGTH;
    }

    case SET_EEV_MAXPULSES_OPEN:
    case SET_EEV_MINWORKPOS: {
      const double bounded = value < 0 ? 0 : value > 255 ? 255 : value;
      return encodeHpCommand(operation == SET_EEV_MAXPULSES_OPEN ? 0x0D : 0x0F,
        static_cast<uint8_t>(std::round(bounded)), buffer);
    }

    case SET_WORKING_WATT: {
      buffer[0] = HP_DEVICE_ID;
      buffer[1] = 0x0E;
      writeScaled(buffer, value, 25599, 1.0);
      buffer[4] = 0xFF;
      return HP_FRAME_LENGTH;
    }
  }

  // An operation added without an encoding must not reach the bus as zeros.
  return 0;
}

bool modbusCrcMatches(const uint8_t *buffer, size_t length)
{
  if (buffer == nullptr || length < 4) return false;

  const uint16_t crc = modbusCrc(buffer, length - 2);
  const bool highByteFirst = buffer[length - 2] == high(crc)
    && buffer[length - 1] == low(crc);
  const bool lowByteFirst = buffer[length - 2] == low(crc)
    && buffer[length - 1] == high(crc);
  return highByteFirst || lowByteFirst;
}
