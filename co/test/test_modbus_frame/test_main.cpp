#ifdef ARDUINO
#include <Arduino.h>
#endif
#include <unity.h>

#include <modbus_frame.hpp>
#include "../../src/modbus_frame.cpp"

namespace {
uint16_t crcOfFrame(const uint8_t *buffer, size_t length)
{
  // Modbus RTU sends the CRC low byte first, so the value is reassembled the
  // other way round.
  return static_cast<uint16_t>(buffer[length - 1] << 8) | buffer[length - 2];
}

void testCrcMatchesTheStandardCheckValue()
{
  // CRC-16/MODBUS is defined by this check value, so a wrong implementation
  // cannot pass here by accident.
  const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX16(0x4B37, modbusCrc(check, sizeof(check)));

  // Canonical request 01 03 00 00 00 0A, whose CRC value is 0xCDC5.
  const uint8_t request[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
  TEST_ASSERT_EQUAL_HEX16(0xCDC5, modbusCrc(request, sizeof(request)));
}

void testHeatPumpCommandsUseTheFiveByteFrame()
{
  uint8_t frame[MODBUS_FRAME_CAPACITY];

  TEST_ASSERT_EQUAL_UINT32(5,
    encodeCommand(GET_HP_DATA, 0, frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_HEX8(0x41, frame[0]);
  TEST_ASSERT_EQUAL_HEX8(0x01, frame[1]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, frame[4]);

  TEST_ASSERT_EQUAL_UINT32(5,
    encodeCommand(SET_HP_CO_ON, 0, frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_HEX8(0x0C, frame[1]);
  TEST_ASSERT_EQUAL_HEX8(1, frame[2]);

  TEST_ASSERT_EQUAL_UINT32(5,
    encodeCommand(SET_HP_CO_OFF, 0, frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_HEX8(0x0C, frame[1]);
  TEST_ASSERT_EQUAL_HEX8(0, frame[2]);
}

void testEveryOnOffPairKeepsItsFunctionCode()
{
  struct Pair {
    SERIAL_OPERATION on;
    SERIAL_OPERATION off;
    uint8_t function;
  };
  const Pair pairs[] = {
    {SET_HP_FORCE_ON, SET_HP_FORCE_OFF, 0x03},
    {SET_HP_CO_ON, SET_HP_CO_OFF, 0x0C},
    {SET_SUMP_HEATER_ON, SET_SUMP_HEATER_OFF, 0x0B},
    {SET_COLD_PUMP_ON, SET_COLD_PUMP_OFF, 0x0A},
    {SET_HOT_PUMP_ON, SET_HOT_PUMP_OFF, 0x09},
  };

  uint8_t frame[MODBUS_FRAME_CAPACITY];
  for (const Pair &pair : pairs) {
    TEST_ASSERT_EQUAL_UINT32(5,
      encodeCommand(pair.on, 0, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_HEX8(pair.function, frame[1]);
    TEST_ASSERT_EQUAL_HEX8(1, frame[2]);

    TEST_ASSERT_EQUAL_UINT32(5,
      encodeCommand(pair.off, 0, frame, sizeof(frame)));
    TEST_ASSERT_EQUAL_HEX8(pair.function, frame[1]);
    TEST_ASSERT_EQUAL_HEX8(0, frame[2]);
  }
}

void testSetpointIsSplitIntoUnitsAndHundredths()
{
  uint8_t frame[MODBUS_FRAME_CAPACITY];

  TEST_ASSERT_EQUAL_UINT32(5,
    encodeCommand(SET_T_SETPOINT_CO, 45.5, frame, sizeof(frame)));
  TEST_ASSERT_EQUAL_HEX8(0x04, frame[1]);
  TEST_ASSERT_EQUAL_UINT8(45, frame[2]);
  TEST_ASSERT_EQUAL_UINT8(50, frame[3]);

  encodeCommand(SET_T_DELTA_CO, 10.0, frame, sizeof(frame));
  TEST_ASSERT_EQUAL_HEX8(0x05, frame[1]);
  TEST_ASSERT_EQUAL_UINT8(10, frame[2]);
  TEST_ASSERT_EQUAL_UINT8(0, frame[3]);

  encodeCommand(SET_EEV_SETPOINT, 3.25, frame, sizeof(frame));
  TEST_ASSERT_EQUAL_HEX8(0x08, frame[1]);
  TEST_ASSERT_EQUAL_UINT8(3, frame[2]);
  TEST_ASSERT_EQUAL_UINT8(25, frame[3]);
}

void testOutOfRangeValuesAreClamped()
{
  uint8_t frame[MODBUS_FRAME_CAPACITY];

  encodeCommand(SET_T_SETPOINT_CO, -10.0, frame, sizeof(frame));
  TEST_ASSERT_EQUAL_UINT8(0, frame[2]);
  TEST_ASSERT_EQUAL_UINT8(0, frame[3]);

  encodeCommand(SET_T_SETPOINT_CO, 900.0, frame, sizeof(frame));
  TEST_ASSERT_EQUAL_UINT8(255, frame[2]);
  TEST_ASSERT_EQUAL_UINT8(99, frame[3]);

  encodeCommand(SET_WORKING_WATT, 1840.0, frame, sizeof(frame));
  TEST_ASSERT_EQUAL_HEX8(0x0E, frame[1]);
  TEST_ASSERT_EQUAL_UINT8(18, frame[2]);
  TEST_ASSERT_EQUAL_UINT8(40, frame[3]);

  encodeCommand(SET_WORKING_WATT, 99999.0, frame, sizeof(frame));
  TEST_ASSERT_EQUAL_UINT8(255, frame[2]);
  TEST_ASSERT_EQUAL_UINT8(99, frame[3]);

  encodeCommand(SET_EEV_MAXPULSES_OPEN, 300.0, frame, sizeof(frame));
  TEST_ASSERT_EQUAL_HEX8(0x0D, frame[1]);
  TEST_ASSERT_EQUAL_UINT8(255, frame[2]);

  encodeCommand(SET_EEV_MAXPULSES_OPEN, -5.0, frame, sizeof(frame));
  TEST_ASSERT_EQUAL_UINT8(0, frame[2]);

  encodeCommand(SET_EEV_MINWORKPOS, 45.0, frame, sizeof(frame));
  TEST_ASSERT_EQUAL_HEX8(0x41, frame[0]);
  TEST_ASSERT_EQUAL_HEX8(0x0F, frame[1]);
  TEST_ASSERT_EQUAL_UINT8(45, frame[2]);
  TEST_ASSERT_EQUAL_HEX8(0xFF, frame[4]);

  encodeCommand(SET_EEV_MINWORKPOS, 300.0, frame, sizeof(frame));
  TEST_ASSERT_EQUAL_UINT8(255, frame[2]);
}

void testPvReadsCoverConsecutiveRegisterBlocks()
{
  uint8_t first[MODBUS_FRAME_CAPACITY];
  uint8_t second[MODBUS_FRAME_CAPACITY];

  TEST_ASSERT_EQUAL_UINT32(8,
    encodeCommand(GET_PV_DATA_1, 0, first, sizeof(first)));
  TEST_ASSERT_EQUAL_UINT32(8,
    encodeCommand(GET_PV_DATA_2, 0, second, sizeof(second)));

  TEST_ASSERT_EQUAL_HEX8(PV_DEVICE_ID, first[0]);
  TEST_ASSERT_EQUAL_HEX8(0x03, first[1]);

  const uint16_t firstStart = (first[2] << 8) | first[3];
  const uint16_t firstCount = (first[4] << 8) | first[5];
  const uint16_t secondStart = (second[2] << 8) | second[3];
  const uint16_t secondCount = (second[4] << 8) | second[5];

  TEST_ASSERT_EQUAL_UINT16(PV_FIRST_REGISTER, firstStart);
  TEST_ASSERT_EQUAL_UINT16(
    PV_DEVICES_PER_REQUEST * PV_REGISTERS_PER_DEVICE, firstCount);

  // The second block starts where the first one ends, counted in registers.
  TEST_ASSERT_EQUAL_UINT16(firstStart + firstCount, secondStart);
  TEST_ASSERT_EQUAL_UINT16(firstCount, secondCount);

  // Function code 3 is limited to 125 registers per request.
  TEST_ASSERT_TRUE(firstCount <= 125);
  TEST_ASSERT_TRUE(secondCount <= 125);
}

void testPvReadsCarryAValidCrcInModbusByteOrder()
{
  uint8_t frame[MODBUS_FRAME_CAPACITY];
  const size_t length = encodeCommand(GET_PV_DATA_1, 0, frame, sizeof(frame));

  TEST_ASSERT_TRUE(modbusCrcMatches(frame, length));

  // The canonical request 01 03 00 00 00 0A ends with C5 CD on the wire, so
  // the low byte has to come first.
  const uint8_t reference[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD};
  TEST_ASSERT_TRUE(modbusCrcMatches(reference, sizeof(reference)));

  uint8_t swapped[MODBUS_FRAME_CAPACITY];
  for (size_t index = 0; index < length; index++) swapped[index] = frame[index];
  const uint8_t keep = swapped[length - 2];
  swapped[length - 2] = swapped[length - 1];
  swapped[length - 1] = keep;
  TEST_ASSERT_EQUAL_UINT16(crcOfFrame(frame, length),
    static_cast<uint16_t>((swapped[length - 2] << 8) | swapped[length - 1]));
}

void testCorruptedFramesFailTheCrcCheck()
{
  uint8_t frame[MODBUS_FRAME_CAPACITY];
  const size_t length = encodeCommand(GET_PV_DATA_1, 0, frame, sizeof(frame));

  frame[3] = static_cast<uint8_t>(frame[3] + 1);
  TEST_ASSERT_FALSE(modbusCrcMatches(frame, length));

  TEST_ASSERT_FALSE(modbusCrcMatches(nullptr, length));
  TEST_ASSERT_FALSE(modbusCrcMatches(frame, 3));
}

void testTooSmallBufferProducesNothing()
{
  uint8_t frame[MODBUS_FRAME_CAPACITY];
  TEST_ASSERT_EQUAL_UINT32(0,
    encodeCommand(GET_PV_DATA_1, 0, frame, MODBUS_FRAME_CAPACITY - 1));
  TEST_ASSERT_EQUAL_UINT32(0, encodeCommand(GET_HP_DATA, 0, nullptr, 8));
}
}

int runAllTests()
{
  UNITY_BEGIN();
  RUN_TEST(testCrcMatchesTheStandardCheckValue);
  RUN_TEST(testHeatPumpCommandsUseTheFiveByteFrame);
  RUN_TEST(testEveryOnOffPairKeepsItsFunctionCode);
  RUN_TEST(testSetpointIsSplitIntoUnitsAndHundredths);
  RUN_TEST(testOutOfRangeValuesAreClamped);
  RUN_TEST(testPvReadsCoverConsecutiveRegisterBlocks);
  RUN_TEST(testPvReadsCarryAValidCrcInModbusByteOrder);
  RUN_TEST(testCorruptedFramesFailTheCrcCheck);
  RUN_TEST(testTooSmallBufferProducesNothing);
  return UNITY_END();
}

#ifdef ARDUINO
void setup()
{
  // The runner needs the serial link up before the first report.
  delay(2000);
  runAllTests();
}

void loop()
{
}
#else
int main()
{
  return runAllTests();
}
#endif
