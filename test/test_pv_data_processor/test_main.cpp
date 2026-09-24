#ifdef ARDUINO
#include <Arduino.h>
#endif
#include <unity.h>

#include <cstring>

#include <pv_data_processor.hpp>
#include "../../src/pv_data_processor.cpp"

namespace {
// Values as they sit in a DTU record, before any scaling.
struct PvRecord {
  const char *serial;   // twelve decimal digits, stored as six BCD bytes
  uint8_t port;
  uint16_t power;       // 0.1 W
  uint16_t today;       // Wh
  uint32_t total;       // Wh
  int16_t temperature;  // 0.1 degrees Celsius
};

constexpr size_t HEADER_LENGTH = 3;

void writeBe(uint8_t *out, uint32_t value, uint8_t byteCount)
{
  for (uint8_t index = 0; index < byteCount; index++) {
    out[index] = static_cast<uint8_t>(value >> (8 * (byteCount - 1 - index)));
  }
}

// Assembles a Modbus response the way the DTU is understood to send it:
// address, function code, byte count, then one 40-byte record per port.
size_t buildFrame(uint8_t *out, const PvRecord *records, uint8_t count)
{
  const size_t payload = count * PV_BYTES_PER_DEVICE;
  memset(out, 0, HEADER_LENGTH + payload);
  out[0] = PV_DEVICE_ID;
  out[1] = 0x03;
  out[2] = static_cast<uint8_t>(payload);

  for (uint8_t index = 0; index < count; index++) {
    uint8_t *record = out + HEADER_LENGTH + index * PV_BYTES_PER_DEVICE;
    record[0] = 0x01;
    for (uint8_t digit = 0; digit < 6; digit++) {
      record[1 + digit] = static_cast<uint8_t>(
        ((records[index].serial[digit * 2] - '0') << 4)
        | (records[index].serial[digit * 2 + 1] - '0'));
    }
    record[7] = records[index].port;
    writeBe(record + 16, records[index].power, 2);
    writeBe(record + 18, records[index].today, 2);
    writeBe(record + 20, records[index].total, 4);
    writeBe(record + 24, static_cast<uint16_t>(records[index].temperature), 2);
  }
  return HEADER_LENGTH + payload;
}

void testFrameIsParsedIntoPanelsAndSums()
{
  const PvRecord first[] = {
    {"114172035403", 1, 3120, 870, 521340, 305},
    {"114172035403", 2, 2980, 810, 498120, 315},
  };
  const PvRecord second[] = {
    {"114172099001", 1, 1900, 540, 310500, 295},
    {"114172099001", 2, 2000, 560, 320000, 285},
  };

  uint8_t frame[256];
  PvDataProcessor processor;

  TEST_ASSERT_TRUE(processor.appendFrame(frame, buildFrame(frame, first, 2)));
  TEST_ASSERT_TRUE(processor.appendFrame(frame, buildFrame(frame, second, 2)));

  PV pv;
  TEST_ASSERT_TRUE(processor.complete(pv, 2000));

  // Power arrives in 0.1 W and is truncated to whole watts per port.
  TEST_ASSERT_EQUAL_INT64(312 + 298 + 190 + 200, pv.total_power);
  TEST_ASSERT_EQUAL_UINT64(870 + 810 + 540 + 560, pv.total_prod_today);
  TEST_ASSERT_EQUAL_UINT64(521340 + 498120 + 310500 + 320000, pv.total_prod);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.0f, pv.temperature);

  TEST_ASSERT_EQUAL_UINT8(4, pv.panel_count);
  TEST_ASSERT_EQUAL_STRING("114172035403", pv.panels[0].inverter_serial);
  TEST_ASSERT_EQUAL_UINT8(1, pv.panels[0].port);
  TEST_ASSERT_EQUAL_INT32(312, pv.panels[0].power);
  TEST_ASSERT_EQUAL_UINT32(870, pv.panels[0].prod_today);
  TEST_ASSERT_EQUAL_UINT32(521340, pv.panels[0].prod_total);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 30.5f, pv.panels[0].temperature);

  // The second response continues the port order of the first one.
  TEST_ASSERT_EQUAL_STRING("114172099001", pv.panels[2].inverter_serial);
  TEST_ASSERT_EQUAL_INT32(190, pv.panels[2].power);
}

void testRecordCountComesFromTheByteCountField()
{
  const PvRecord records[] = {
    {"114172035403", 1, 1000, 10, 100, 250},
    {"114172035403", 2, 2000, 20, 200, 250},
    {"114172035403", 3, 3000, 30, 300, 250},
  };

  uint8_t frame[256];
  PvDataProcessor processor;
  const size_t length = buildFrame(frame, records, 3);

  // A response shorter than PV_DEVICES_PER_REQUEST must still be accepted.
  TEST_ASSERT_TRUE(processor.appendFrame(frame, length));
  TEST_ASSERT_TRUE(processor.appendFrame(frame, length));

  PV pv;
  TEST_ASSERT_TRUE(processor.complete(pv, 2000));
  TEST_ASSERT_EQUAL_UINT8(6, pv.panel_count);
  TEST_ASSERT_EQUAL_INT64((100 + 200 + 300) * 2, pv.total_power);
}

void testNegativeTemperatureIsReadAsSigned()
{
  const PvRecord records[] = {
    {"114172035403", 1, 0, 0, 0, -55},
    {"114172035403", 2, 0, 0, 0, -45},
  };

  uint8_t frame[256];
  PvDataProcessor processor;
  const size_t length = buildFrame(frame, records, 2);
  TEST_ASSERT_TRUE(processor.appendFrame(frame, length));
  TEST_ASSERT_TRUE(processor.appendFrame(frame, length));

  PV pv;
  TEST_ASSERT_TRUE(processor.complete(pv, 2000));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.0f, pv.temperature);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.5f, pv.panels[0].temperature);
}

void testMalformedFramesAreRejected()
{
  const PvRecord records[] = {{"114172035403", 1, 1000, 10, 100, 250}};
  uint8_t frame[256];
  const size_t length = buildFrame(frame, records, 1);

  PvDataProcessor processor;
  TEST_ASSERT_FALSE(processor.appendFrame(nullptr, length));
  TEST_ASSERT_FALSE(processor.appendFrame(frame, HEADER_LENGTH));

  // Byte count that is not a whole number of records.
  uint8_t odd[256];
  memcpy(odd, frame, length);
  odd[2] = PV_BYTES_PER_DEVICE - 1;
  TEST_ASSERT_FALSE(processor.appendFrame(odd, length));

  // Byte count larger than the number of bytes actually received.
  uint8_t truncated[256];
  memcpy(truncated, frame, length);
  truncated[2] = PV_BYTES_PER_DEVICE * 2;
  TEST_ASSERT_FALSE(processor.appendFrame(truncated, length));

  uint8_t empty[256];
  memcpy(empty, frame, length);
  empty[2] = 0;
  TEST_ASSERT_FALSE(processor.appendFrame(empty, length));
}

void testCompleteRequiresEveryExpectedFrame()
{
  const PvRecord records[] = {{"114172035403", 1, 1000, 10, 100, 250}};
  uint8_t frame[256];
  const size_t length = buildFrame(frame, records, 1);

  PvDataProcessor processor;
  PV pv;

  TEST_ASSERT_FALSE(processor.complete(pv, 2000));
  TEST_ASSERT_TRUE(processor.appendFrame(frame, length));
  TEST_ASSERT_FALSE(processor.complete(pv, 2000));
  TEST_ASSERT_TRUE(processor.appendFrame(frame, length));
  TEST_ASSERT_TRUE(processor.complete(pv, 2000));

  // Nothing beyond the expected number of responses is accepted.
  TEST_ASSERT_FALSE(processor.appendFrame(frame, length));
}

void testPvPowerFollowsTheForceThreshold()
{
  const PvRecord records[] = {{"114172035403", 1, 12000, 0, 0, 250}};
  uint8_t frame[256];
  const size_t length = buildFrame(frame, records, 1);

  PvDataProcessor processor;
  TEST_ASSERT_TRUE(processor.appendFrame(frame, length));
  TEST_ASSERT_TRUE(processor.appendFrame(frame, length));

  PV pv;
  TEST_ASSERT_TRUE(processor.complete(pv, 2000));
  TEST_ASSERT_EQUAL_INT64(2400, pv.total_power);
  TEST_ASSERT_TRUE(pv.pv_power);

  TEST_ASSERT_TRUE(processor.complete(pv, 2401));
  TEST_ASSERT_FALSE(pv.pv_power);
}

void testResetDiscardsPreviousFrames()
{
  const PvRecord records[] = {{"114172035403", 1, 1000, 10, 100, 250}};
  uint8_t frame[256];
  const size_t length = buildFrame(frame, records, 1);

  PvDataProcessor processor;
  TEST_ASSERT_TRUE(processor.appendFrame(frame, length));
  processor.reset();

  PV pv;
  TEST_ASSERT_FALSE(processor.complete(pv, 2000));
  TEST_ASSERT_TRUE(processor.appendFrame(frame, length));
  TEST_ASSERT_TRUE(processor.appendFrame(frame, length));
  TEST_ASSERT_TRUE(processor.complete(pv, 2000));
  TEST_ASSERT_EQUAL_INT64(200, pv.total_power);
  TEST_ASSERT_EQUAL_UINT8(2, pv.panel_count);
}
}

int runAllTests()
{
  UNITY_BEGIN();
  RUN_TEST(testFrameIsParsedIntoPanelsAndSums);
  RUN_TEST(testRecordCountComesFromTheByteCountField);
  RUN_TEST(testNegativeTemperatureIsReadAsSigned);
  RUN_TEST(testMalformedFramesAreRejected);
  RUN_TEST(testCompleteRequiresEveryExpectedFrame);
  RUN_TEST(testPvPowerFollowsTheForceThreshold);
  RUN_TEST(testResetDiscardsPreviousFrames);
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
