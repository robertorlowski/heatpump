#ifdef ARDUINO
#include <Arduino.h>
#endif
#include <unity.h>

#include <access_point_policy.hpp>
#include "../../src/access_point_policy.cpp"

namespace {
constexpr unsigned long SECOND = 1000UL;
constexpr unsigned long MINUTE = 60UL * SECOND;

// Keeps the cloud answering every 30 s, as the telemetry does while idle.
bool runOnline(AccessPointPolicy &policy, unsigned long from, unsigned long to)
{
  bool enabled = policy.enabled();
  for (unsigned long now = from; now <= to; now += 30 * SECOND) {
    enabled = policy.update(now, true, true, now);
  }
  return enabled;
}

// Brings the policy to the state with the access point switched off.
unsigned long switchOff(AccessPointPolicy &policy)
{
  const unsigned long end = AccessPointPolicy::STABLE_ONLINE_MS + MINUTE;
  runOnline(policy, 0, end);
  TEST_ASSERT_FALSE(policy.enabled());
  return end;
}
}

void setUp() {}
void tearDown() {}

void testAccessPointStartsEnabled()
{
  AccessPointPolicy policy;
  TEST_ASSERT_TRUE(policy.enabled());
  TEST_ASSERT_TRUE(policy.update(0, false, false, 0));
}

void testStationWithoutCloudAnswerKeepsAccessPoint()
{
  // An address alone does not prove internet access.
  AccessPointPolicy policy;
  for (unsigned long now = 0; now <= 10 * MINUTE; now += 30 * SECOND) {
    TEST_ASSERT_TRUE(policy.update(now, true, false, 0));
  }
}

void testAccessPointGoesDownAfterThreeStableMinutes()
{
  AccessPointPolicy policy;
  TEST_ASSERT_TRUE(runOnline(policy, 0, 2 * MINUTE + 30 * SECOND));
  TEST_ASSERT_FALSE(policy.update(3 * MINUTE, true, true, 3 * MINUTE));
}

void testFailedRequestRestartsTheStablePeriod()
{
  AccessPointPolicy policy;
  runOnline(policy, 0, 2 * MINUTE);
  TEST_ASSERT_TRUE(policy.update(2 * MINUTE + 30 * SECOND, true, false,
    2 * MINUTE));
  // Three minutes since the start, but only 30 s since the miss.
  TEST_ASSERT_TRUE(runOnline(policy, 3 * MINUTE, 5 * MINUTE));
  TEST_ASSERT_FALSE(runOnline(policy, 5 * MINUTE + 30 * SECOND, 6 * MINUTE));
}

void testShortStationDropKeepsAccessPointOff()
{
  AccessPointPolicy policy;
  const unsigned long off = switchOff(policy);
  TEST_ASSERT_FALSE(policy.update(off + 10 * SECOND, false, false, off));
  TEST_ASSERT_FALSE(policy.update(off + 50 * SECOND, false, false, off));
  TEST_ASSERT_FALSE(policy.update(off + MINUTE, true, true, off + MINUTE));
}

void testStationLostForAMinuteRestoresAccessPoint()
{
  AccessPointPolicy policy;
  const unsigned long off = switchOff(policy);
  TEST_ASSERT_FALSE(policy.update(off + 10 * SECOND, false, false, off));
  TEST_ASSERT_TRUE(policy.update(off + 70 * SECOND, false, false, off));
}

void testSingleFailedRequestKeepsAccessPointOff()
{
  AccessPointPolicy policy;
  const unsigned long off = switchOff(policy);
  TEST_ASSERT_FALSE(policy.update(off + 30 * SECOND, true, false, off));
}

void testSilentCloudForFiveMinutesRestoresAccessPoint()
{
  AccessPointPolicy policy;
  const unsigned long off = switchOff(policy);
  TEST_ASSERT_FALSE(policy.update(off + 4 * MINUTE, true, false, off));
  TEST_ASSERT_TRUE(policy.update(off + 5 * MINUTE, true, false, off));
}

void testStaleAnswerDoesNotSwitchAccessPointOffAgain()
{
  // No request is sent any more, so the last one stays "answered".
  AccessPointPolicy policy;
  const unsigned long off = switchOff(policy);
  const unsigned long restored = off + 5 * MINUTE;
  TEST_ASSERT_TRUE(policy.update(restored, true, true, off));
  TEST_ASSERT_TRUE(policy.update(restored + 10 * MINUTE, true, true, off));
}

void testAccessPointGoesDownAgainAfterRecovery()
{
  AccessPointPolicy policy;
  const unsigned long off = switchOff(policy);
  const unsigned long lost = off + 2 * MINUTE;
  TEST_ASSERT_FALSE(policy.update(off + 10 * SECOND, false, false, off));
  TEST_ASSERT_TRUE(policy.update(lost, false, false, off));

  TEST_ASSERT_TRUE(runOnline(policy, lost + MINUTE, lost + 3 * MINUTE));
  TEST_ASSERT_FALSE(policy.update(lost + 4 * MINUTE, true, true,
    lost + 4 * MINUTE));
}

void testMillisWrapDoesNotBreakTheTimers()
{
  AccessPointPolicy policy;
  const unsigned long start = static_cast<unsigned long>(-1) - MINUTE;
  unsigned long now = start;
  for (int step = 0; step < 6; step++, now += 30 * SECOND) {
    TEST_ASSERT_TRUE(policy.update(now, true, true, now));
  }
  TEST_ASSERT_FALSE(policy.update(now, true, true, now));
}

int runAllTests()
{
  UNITY_BEGIN();
  RUN_TEST(testAccessPointStartsEnabled);
  RUN_TEST(testStationWithoutCloudAnswerKeepsAccessPoint);
  RUN_TEST(testAccessPointGoesDownAfterThreeStableMinutes);
  RUN_TEST(testFailedRequestRestartsTheStablePeriod);
  RUN_TEST(testShortStationDropKeepsAccessPointOff);
  RUN_TEST(testStationLostForAMinuteRestoresAccessPoint);
  RUN_TEST(testSingleFailedRequestKeepsAccessPointOff);
  RUN_TEST(testSilentCloudForFiveMinutesRestoresAccessPoint);
  RUN_TEST(testStaleAnswerDoesNotSwitchAccessPointOffAgain);
  RUN_TEST(testAccessPointGoesDownAgainAfterRecovery);
  RUN_TEST(testMillisWrapDoesNotBreakTheTimers);
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
