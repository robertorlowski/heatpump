#pragma once

#include <cstddef>
#include <cstdint>

#include <domain_types.hpp>

// Longest frame encodeCommand() can produce.
constexpr size_t MODBUS_FRAME_CAPACITY = 8;

// Writes the wire representation of a command into buffer and returns its
// length, or zero when the operation has no encoding or the buffer is too
// small. Kept free of HardwareSerial so the wire format can be tested.
size_t encodeCommand(SERIAL_OPERATION operation, double value,
  uint8_t *buffer, size_t capacity);

// CRC-16/MODBUS: polynomial 0xA001 reflected, seed 0xFFFF, no final xor.
// The check value for the string "123456789" is 0x4B37.
uint16_t modbusCrc(const uint8_t *data, size_t length);

// True when the trailing two bytes match the CRC of everything before them.
// Both byte orders are accepted, because the transmitted order has never been
// confirmed against the inverter.
bool modbusCrcMatches(const uint8_t *buffer, size_t length);
