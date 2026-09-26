#ifdef ARDUINO
#include <Arduino.h>
#endif
#include <unity.h>

#include <cop_estimator.hpp>
#include <operation_controller.hpp>
#include <operation_parser.hpp>
#include "../../src/cop_estimator.cpp"
#include "../../src/operation_controller.cpp"
#include "../../src/operation_parser.cpp"

namespace {
struct RecordedCommand {
  SERIAL_OPERATION operation;
  double value;
};

class RecordingSink : public CommandSink {
public:
  bool enqueue(SERIAL_OPERATION operation, double value = 0.0) override
  {
    return record(operation, value, false);
  }

  bool enqueuePriority(SERIAL_OPERATION operation, double value = 0.0) override
  {
    return record(operation, value, true);
  }

  bool record(SERIAL_OPERATION operation, double value, bool priority)
  {
    if (!acceptCommands) return false;
    if (count >= CAPACITY) return false;
    commands[count++] = {operation, value};
    priorities[count - 1] = priority;
    return true;
  }

  void clear()
  {
    count = 0;
  }

  static constexpr size_t CAPACITY = 32;
  RecordedCommand commands[CAPACITY]{};
  bool priorities[CAPACITY]{};
  size_t count = 0;
  bool acceptCommands = true;
};

ServerOperationState modePatch(WORK_MODE mode)
{
  ServerOperationState patch;
  patch.workMode.present = true;
  patch.workMode.value = mode;
  return patch;
}

void testRepeatedOperationDoesNotScheduleCommandsAgain()
{
  RecordingSink sink;
  OperationController controller(sink);
  ServerOperationState patch = modePatch(WORK_MODE::AUTO);

  controller.applyServerPatch(patch);
  TEST_ASSERT_EQUAL_UINT32(3, sink.count);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_HP_CO_ON, sink.commands[0].operation);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_T_SETPOINT_CO, sink.commands[1].operation);
  TEST_ASSERT_EQUAL_DOUBLE(45.0, sink.commands[1].value);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_T_DELTA_CO, sink.commands[2].operation);
  TEST_ASSERT_EQUAL_DOUBLE(10.0, sink.commands[2].value);

  sink.clear();
  controller.applyServerPatch(patch);
  TEST_ASSERT_EQUAL_UINT32(0, sink.count);
}

void testEmptyPatchDoesNotApplyDefaults()
{
  RecordingSink sink;
  OperationController controller(sink);
  ServerOperationState empty;

  controller.applyServerPatch(empty);

  TEST_ASSERT_EQUAL_UINT32(0, sink.count);
}

void testOnlyChangedServerFieldIsScheduled()
{
  RecordingSink sink;
  OperationController controller(sink);
  ServerOperationState patch = modePatch(WORK_MODE::AUTO);
  patch.hotPump.present = true;
  patch.hotPump.value = true;
  controller.applyServerPatch(patch);

  sink.clear();
  controller.applyServerPatch(patch);
  TEST_ASSERT_EQUAL_UINT32(0, sink.count);

  ServerOperationState changed;
  changed.hotPump.present = true;
  changed.hotPump.value = false;
  controller.applyServerPatch(changed);

  TEST_ASSERT_EQUAL_UINT32(1, sink.count);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_HOT_PUMP_OFF,
    sink.commands[0].operation);
}

void testOffHasPriorityAndIsNotRepeated()
{
  RecordingSink sink;
  OperationController controller(sink);
  ServerOperationState running = modePatch(WORK_MODE::AUTO);
  running.force.present = true;
  running.force.value = true;
  running.hotPump.present = true;
  running.hotPump.value = true;
  controller.applyServerPatch(running);

  sink.clear();
  ServerOperationState off = modePatch(WORK_MODE::OFF);
  controller.applyServerPatch(off);

  TEST_ASSERT_EQUAL_UINT32(4, sink.count);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_HP_CO_OFF, sink.commands[0].operation);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_HP_FORCE_OFF, sink.commands[1].operation);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_HOT_PUMP_OFF, sink.commands[2].operation);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_COLD_PUMP_OFF, sink.commands[3].operation);
  TEST_ASSERT_TRUE(sink.priorities[0]);
  TEST_ASSERT_TRUE(sink.priorities[1]);
  TEST_ASSERT_TRUE(sink.priorities[2]);
  TEST_ASSERT_TRUE(sink.priorities[3]);
  TEST_ASSERT_FALSE(controller.coRelay());
  TEST_ASSERT_FALSE(controller.cwuRelay());

  sink.clear();
  controller.applyServerPatch(off);
  TEST_ASSERT_EQUAL_UINT32(0, sink.count);
}

void testRelaysFollowServerCoPumpState()
{
  RecordingSink sink;
  OperationController controller(sink);
  ServerOperationState patch = modePatch(WORK_MODE::AUTO);
  patch.coPump.present = true;
  patch.coPump.value = false;

  controller.applyServerPatch(patch);

  TEST_ASSERT_FALSE(controller.coRelay());
  TEST_ASSERT_FALSE(controller.cwuRelay());
}

void testCwuModeDisablesLocalRelays()
{
  RecordingSink sink;
  OperationController controller(sink);
  ServerOperationState running = modePatch(WORK_MODE::AUTO);
  running.coPump.present = true;
  running.coPump.value = true;
  controller.applyServerPatch(running);
  TEST_ASSERT_TRUE(controller.coRelay());
  TEST_ASSERT_TRUE(controller.cwuRelay());

  controller.applyServerPatch(modePatch(WORK_MODE::CWU));

  TEST_ASSERT_FALSE(controller.coRelay());
  TEST_ASSERT_FALSE(controller.cwuRelay());
}

void testLocalOffIsIndependentFromCloudWorkMode()
{
  RecordingSink sink;
  OperationController controller(sink);
  controller.applyServerPatch(modePatch(WORK_MODE::AUTO));
  sink.clear();

  controller.setControllerMode(ControllerMode::OFF);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(ControllerMode::OFF),
    static_cast<int>(controller.controllerMode()));
  TEST_ASSERT_EQUAL_INT(WORK_MODE::AUTO, controller.preferences().workMode);
  TEST_ASSERT_EQUAL_UINT32(4, sink.count);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_HP_CO_OFF, sink.commands[0].operation);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_HP_FORCE_OFF, sink.commands[1].operation);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_HOT_PUMP_OFF, sink.commands[2].operation);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_COLD_PUMP_OFF, sink.commands[3].operation);
  TEST_ASSERT_FALSE(controller.coRelay());
  TEST_ASSERT_FALSE(controller.cwuRelay());

  controller.applyServerPatch(modePatch(WORK_MODE::CWU));
  TEST_ASSERT_EQUAL_INT(WORK_MODE::AUTO, controller.preferences().workMode);
}

void testManualModeDoesNotSendOrApplyCloudCommands()
{
  RecordingSink sink;
  OperationController controller(sink);
  controller.applyServerPatch(modePatch(WORK_MODE::AUTO));
  sink.clear();

  controller.setControllerMode(ControllerMode::MANUAL_CO);
  controller.applyServerPatch(modePatch(WORK_MODE::CWU));

  PV highProduction;
  highProduction.pv_power = true;
  highProduction.total_power = 5000;
  controller.updatePv(highProduction);
  controller.tick();

  TEST_ASSERT_EQUAL_UINT32(0, sink.count);
  TEST_ASSERT_EQUAL_INT(WORK_MODE::AUTO, controller.preferences().workMode);
  // Both local relays carry the same state, so MANUAL_CO enables the pair.
  TEST_ASSERT_TRUE(controller.coRelay());
  TEST_ASSERT_TRUE(controller.cwuRelay());
}

void testManualCwuDisablesRelaysWithoutSendingCommands()
{
  RecordingSink sink;
  OperationController controller(sink);
  controller.applyServerPatch(modePatch(WORK_MODE::AUTO));
  sink.clear();

  controller.setControllerMode(ControllerMode::MANUAL_CWU);

  TEST_ASSERT_EQUAL_UINT32(0, sink.count);
  // MANUAL_CWU drops the pair; unlike OFF it sends no safety sequence.
  TEST_ASSERT_FALSE(controller.coRelay());
  TEST_ASSERT_FALSE(controller.cwuRelay());
}

void testInvalidTemperatureRangeIsIgnored()
{
  RecordingSink sink;
  OperationController controller(sink);
  controller.applyServerPatch(modePatch(WORK_MODE::AUTO));
  sink.clear();

  ServerOperationState invalid;
  invalid.coMin.present = true;
  invalid.coMin.value = 49;
  controller.applyServerPatch(invalid);

  TEST_ASSERT_EQUAL_DOUBLE(35, controller.preferences().coMin);
  TEST_ASSERT_EQUAL_DOUBLE(45, controller.preferences().coMax);
  TEST_ASSERT_EQUAL_UINT32(0, sink.count);
}

void testRejectedQueueCommandIsRetried()
{
  RecordingSink sink;
  sink.acceptCommands = false;
  OperationController controller(sink);
  controller.applyServerPatch(modePatch(WORK_MODE::AUTO));
  TEST_ASSERT_EQUAL_UINT32(0, sink.count);

  sink.acceptCommands = true;
  controller.tick();

  TEST_ASSERT_EQUAL_UINT32(3, sink.count);
}

void testOperationParserAcceptsTypedValues()
{
  JsonDocument document;
  deserializeJson(document,
    "{\"work_mode\":\"A\",\"force\":1,\"co_max\":46.6}");

  OperationParseResult parsed = parseServerOperation(document.as<JsonObjectConst>());

  TEST_ASSERT_EQUAL_UINT16(0, parsed.invalidValues);
  TEST_ASSERT_TRUE(parsed.state.workMode.present);
  TEST_ASSERT_EQUAL_INT(WORK_MODE::AUTO, parsed.state.workMode.value);
  TEST_ASSERT_TRUE(parsed.state.force.present);
  TEST_ASSERT_TRUE(parsed.state.force.value);
  TEST_ASSERT_TRUE(parsed.state.coMax.present);
  TEST_ASSERT_EQUAL_DOUBLE(47, parsed.state.coMax.value);
}

void testOperationParserRejectsInvalidValues()
{
  JsonDocument document;
  deserializeJson(document,
    "{\"work_mode\":\"UNKNOWN\",\"force\":2,\"working_watt\":30000}");

  OperationParseResult parsed = parseServerOperation(document.as<JsonObjectConst>());

  TEST_ASSERT_EQUAL_UINT16(3, parsed.invalidValues);
  TEST_ASSERT_FALSE(parsed.state.workMode.present);
  TEST_ASSERT_FALSE(parsed.state.force.present);
  TEST_ASSERT_FALSE(parsed.state.workingWatt.present);
}

void testAutoPvChangesForceOnlyAtThresholdTransitions()
{
  RecordingSink sink;
  OperationController controller(sink, 2000);
  controller.applyServerPatch(modePatch(WORK_MODE::AUTO_PV));

  sink.clear();
  PV highProduction;
  highProduction.pv_power = true;
  highProduction.total_power = 2500;
  controller.updatePv(highProduction);
  TEST_ASSERT_EQUAL_UINT32(1, sink.count);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_HP_FORCE_ON,
    sink.commands[0].operation);

  sink.clear();
  controller.updatePv(highProduction);
  TEST_ASSERT_EQUAL_UINT32(0, sink.count);

  PV lowProduction;
  lowProduction.pv_power = false;
  lowProduction.total_power = 1500;
  controller.updatePv(lowProduction);
  TEST_ASSERT_EQUAL_UINT32(1, sink.count);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_HP_FORCE_OFF,
    sink.commands[0].operation);
}

void testPartialPatchPreservesPreviousServerValues()
{
  RecordingSink sink;
  OperationController controller(sink);
  ServerOperationState initial = modePatch(WORK_MODE::AUTO);
  initial.workingWatt.present = true;
  initial.workingWatt.value = 3500;
  controller.applyServerPatch(initial);

  sink.clear();
  ServerOperationState temperatureOnly;
  temperatureOnly.coMax.present = true;
  temperatureOnly.coMax.value = 46;
  controller.applyServerPatch(temperatureOnly);

  TEST_ASSERT_TRUE(controller.serverState().workingWatt.present);
  TEST_ASSERT_EQUAL_DOUBLE(3500, controller.serverState().workingWatt.value);
  TEST_ASSERT_EQUAL_UINT32(2, sink.count);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_T_SETPOINT_CO,
    sink.commands[0].operation);
  TEST_ASSERT_EQUAL_DOUBLE(46, sink.commands[0].value);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_T_DELTA_CO,
    sink.commands[1].operation);
  TEST_ASSERT_EQUAL_DOUBLE(11, sink.commands[1].value);
}

void testEevMaximumIsSentBeforeMinimum()
{
  RecordingSink sink;
  OperationController controller(sink);
  controller.applyServerPatch(modePatch(WORK_MODE::AUTO));

  sink.clear();
  ServerOperationState limits;
  limits.eevMinPulseOpen.present = true;
  limits.eevMinPulseOpen.value = 65;
  limits.eevMaxPulseOpen.present = true;
  limits.eevMaxPulseOpen.value = 80;
  controller.applyServerPatch(limits);

  TEST_ASSERT_EQUAL_UINT32(2, sink.count);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_EEV_MAXPULSES_OPEN,
    sink.commands[0].operation);
  TEST_ASSERT_EQUAL_DOUBLE(80, sink.commands[0].value);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_EEV_MINWORKPOS,
    sink.commands[1].operation);
  TEST_ASSERT_EQUAL_DOUBLE(65, sink.commands[1].value);

  sink.clear();
  ServerOperationState minimumOnly;
  minimumOnly.eevMinPulseOpen.present = true;
  minimumOnly.eevMinPulseOpen.value = 40;
  controller.applyServerPatch(minimumOnly);

  TEST_ASSERT_EQUAL_UINT32(1, sink.count);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_EEV_MINWORKPOS,
    sink.commands[0].operation);
  TEST_ASSERT_EQUAL_DOUBLE(40, sink.commands[0].value);
}

void testMaintenanceActionsAreSentOnceAndNotKept()
{
  RecordingSink sink;
  OperationController controller(sink);
  controller.applyServerPatch(modePatch(WORK_MODE::AUTO));

  sink.clear();
  ServerOperationState reset;
  reset.errorReset.present = true;
  reset.errorReset.value = true;
  controller.applyServerPatch(reset);

  TEST_ASSERT_EQUAL_UINT32(1, sink.count);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::HP_ERROR_RESET, sink.commands[0].operation);
  TEST_ASSERT_TRUE(sink.priorities[0]);
  TEST_ASSERT_FALSE(controller.serverState().errorReset.present);

  // The next ordinary operation does not repeat the action.
  sink.clear();
  controller.applyServerPatch(modePatch(WORK_MODE::AUTO));
  TEST_ASSERT_EQUAL_UINT32(0, sink.count);

  // A second request is a new action and is sent again.
  controller.applyServerPatch(reset);
  TEST_ASSERT_EQUAL_UINT32(1, sink.count);
}

void testRestartResendsTheWholeStateAfterwards()
{
  RecordingSink sink;
  OperationController controller(sink);
  ServerOperationState running = modePatch(WORK_MODE::AUTO);
  running.hotPump.present = true;
  running.hotPump.value = true;
  controller.applyServerPatch(running);

  sink.clear();
  ServerOperationState restart;
  restart.restart.present = true;
  restart.restart.value = true;
  controller.applyServerPatch(restart);

  TEST_ASSERT_EQUAL_UINT32(1, sink.count);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::HP_RESTART, sink.commands[0].operation);

  // CHPC lost its forced pumps, so the same operation is sent in full again.
  sink.clear();
  controller.applyServerPatch(running);
  TEST_ASSERT_TRUE(sink.count >= 4);
  bool hotPumpSent = false;
  for (size_t index = 0; index < sink.count; index++) {
    if (sink.commands[index].operation == SERIAL_OPERATION::SET_HOT_PUMP_ON) hotPumpSent = true;
  }
  TEST_ASSERT_TRUE(hotPumpSent);
}

HeatPumpReport report(bool coOn, bool force, bool running)
{
  HeatPumpReport result;
  result.coOn = coOn;
  result.force = force;
  result.running = running;
  return result;
}

bool wasSent(const RecordingSink &sink, SERIAL_OPERATION operation)
{
  for (size_t index = 0; index < sink.count; index++) {
    if (sink.commands[index].operation == operation) return true;
  }
  return false;
}

void testCoReportedOffIsSentAgain()
{
  RecordingSink sink;
  OperationController controller(sink);
  controller.applyServerPatch(modePatch(WORK_MODE::AUTO));

  // CHPC missed the command, e.g. it was disconnected at the time.
  sink.clear();
  controller.updateHeatPumpReport(report(false, false, false));
  TEST_ASSERT_EQUAL_UINT32(1, sink.count);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_HP_CO_ON, sink.commands[0].operation);

  sink.clear();
  controller.updateHeatPumpReport(report(true, false, false));
  TEST_ASSERT_EQUAL_UINT32(0, sink.count);
}

void testCloudOffKeepsCoOff()
{
  RecordingSink sink;
  OperationController controller(sink);
  controller.applyServerPatch(modePatch(WORK_MODE::OFF));

  sink.clear();
  controller.updateHeatPumpReport(report(true, false, false));
  TEST_ASSERT_EQUAL_UINT32(1, sink.count);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_HP_CO_OFF, sink.commands[0].operation);
  TEST_ASSERT_TRUE(sink.priorities[0]);
}

void testForceIsSentAgainOnlyWhileIdle()
{
  RecordingSink sink;
  OperationController controller(sink);
  ServerOperationState patch = modePatch(WORK_MODE::AUTO);
  patch.force.present = true;
  patch.force.value = true;
  controller.applyServerPatch(patch);

  // Running without force is a normal start; CHPC would ignore force anyway.
  sink.clear();
  controller.updateHeatPumpReport(report(true, false, true));
  TEST_ASSERT_EQUAL_UINT32(0, sink.count);

  // CHPC cleared force when it stopped.
  controller.updateHeatPumpReport(report(true, false, false));
  TEST_ASSERT_EQUAL_UINT32(1, sink.count);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_HP_FORCE_ON, sink.commands[0].operation);
}

void testForceIsNotCheckedBeforeServerSendsIt()
{
  RecordingSink sink;
  OperationController controller(sink);
  controller.applyServerPatch(modePatch(WORK_MODE::AUTO));

  sink.clear();
  controller.updateHeatPumpReport(report(true, true, false));
  TEST_ASSERT_EQUAL_UINT32(0, sink.count);
}

void testLostHeatPumpGetsWholeStateBack()
{
  RecordingSink sink;
  OperationController controller(sink);
  ServerOperationState patch = modePatch(WORK_MODE::AUTO);
  patch.hotPump.present = true;
  patch.hotPump.value = true;
  controller.applyServerPatch(patch);

  sink.clear();
  controller.heatPumpLost();
  controller.updateHeatPumpReport(report(true, false, false));
  TEST_ASSERT_TRUE(wasSent(sink, SERIAL_OPERATION::SET_HP_CO_ON));
  TEST_ASSERT_TRUE(wasSent(sink, SERIAL_OPERATION::SET_T_SETPOINT_CO));
  TEST_ASSERT_TRUE(wasSent(sink, SERIAL_OPERATION::SET_HOT_PUMP_ON));

  // Only the first report after the loss resends everything.
  sink.clear();
  controller.updateHeatPumpReport(report(true, false, false));
  TEST_ASSERT_EQUAL_UINT32(0, sink.count);
}

void testLocalOffTurnsCoOffAgain()
{
  RecordingSink sink;
  OperationController controller(sink);
  controller.setControllerMode(ControllerMode::OFF);

  sink.clear();
  controller.updateHeatPumpReport(report(false, false, false));
  TEST_ASSERT_EQUAL_UINT32(0, sink.count);

  // CO switched back on at the pump itself.
  controller.updateHeatPumpReport(report(true, false, false));
  TEST_ASSERT_TRUE(wasSent(sink, SERIAL_OPERATION::SET_HP_CO_OFF));
}

void testManualModeIgnoresHeatPumpReport()
{
  RecordingSink sink;
  OperationController controller(sink);
  controller.applyServerPatch(modePatch(WORK_MODE::AUTO));
  controller.setControllerMode(ControllerMode::MANUAL_CO);

  sink.clear();
  controller.heatPumpLost();
  controller.updateHeatPumpReport(report(false, false, false));
  TEST_ASSERT_EQUAL_UINT32(0, sink.count);
}

HeatPumpReport temperatureReport(double setpoint, double minimum)
{
  HeatPumpReport result = report(true, false, false);
  result.hasTemperatures = true;
  result.setpoint = setpoint;
  result.minimum = minimum;
  return result;
}

ServerOperationState temperaturePatch(WORK_MODE mode, double coMin, double coMax,
  double cwuMin, double cwuMax)
{
  ServerOperationState patch = modePatch(mode);
  patch.coMin.present = true;
  patch.coMin.value = coMin;
  patch.coMax.present = true;
  patch.coMax.value = coMax;
  patch.cwuMin.present = true;
  patch.cwuMin.value = cwuMin;
  patch.cwuMax.present = true;
  patch.cwuMax.value = cwuMax;
  return patch;
}

void testLostDeltaIsSentAgain()
{
  RecordingSink sink;
  OperationController controller(sink);
  controller.applyServerPatch(temperaturePatch(WORK_MODE::AUTO, 35, 45, 25, 48));

  // The case seen on 2026-09-26: the setpoint arrived, the delta frame did
  // not, so CHPC kept the CWU delta of 23 and reported Tmin 22.
  sink.clear();
  controller.updateHeatPumpReport(temperatureReport(45, 22));
  TEST_ASSERT_EQUAL_UINT32(1, sink.count);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_T_DELTA_CO, sink.commands[0].operation);
  TEST_ASSERT_EQUAL_DOUBLE(10.0, sink.commands[0].value);

  sink.clear();
  controller.updateHeatPumpReport(temperatureReport(45, 35));
  TEST_ASSERT_EQUAL_UINT32(0, sink.count);
}

void testLostSetpointIsSentAgainInCwuMode()
{
  RecordingSink sink;
  OperationController controller(sink);
  controller.applyServerPatch(temperaturePatch(WORK_MODE::CWU, 35, 45, 25, 48));

  // Delta 23 already matches, only the setpoint of the previous mode is left.
  sink.clear();
  controller.updateHeatPumpReport(temperatureReport(45, 22));
  TEST_ASSERT_EQUAL_UINT32(1, sink.count);
  TEST_ASSERT_EQUAL_INT(SERIAL_OPERATION::SET_T_SETPOINT_CO, sink.commands[0].operation);
  TEST_ASSERT_EQUAL_DOUBLE(48.0, sink.commands[0].value);
}

void testRoundedTemperaturesAreNotSentAgain()
{
  RecordingSink sink;
  OperationController controller(sink);
  controller.applyServerPatch(temperaturePatch(WORK_MODE::AUTO, 35.25, 45.75, 25, 48));

  // CHPC prints one decimal.
  sink.clear();
  controller.updateHeatPumpReport(temperatureReport(45.8, 35.3));
  TEST_ASSERT_EQUAL_UINT32(0, sink.count);
}

void testDeltaChpcRejectsIsNotSentAgain()
{
  RecordingSink sink;
  OperationController controller(sink);
  // Delta 35 is above the CHPC limit of 30, so CHPC keeps its old one.
  controller.applyServerPatch(temperaturePatch(WORK_MODE::AUTO, 10, 45, 25, 48));

  sink.clear();
  controller.updateHeatPumpReport(temperatureReport(45, 22));
  TEST_ASSERT_EQUAL_UINT32(0, sink.count);
}

void testTemperaturesAreNotCheckedInOffMode()
{
  RecordingSink sink;
  OperationController controller(sink);
  controller.applyServerPatch(temperaturePatch(WORK_MODE::OFF, 35, 45, 25, 48));

  sink.clear();
  HeatPumpReport offReport = temperatureReport(45, 22);
  offReport.coOn = false;
  controller.updateHeatPumpReport(offReport);
  TEST_ASSERT_EQUAL_UINT32(0, sink.count);
}

void testReportedTemperatureStringsAreParsed()
{
  // CHPC sends every value as a string; main.cpp relies on as<double>().
  JsonDocument document;
  deserializeJson(document, R"({"Tmax":"45.0","Tmin":"22.0"})");
  TEST_ASSERT_EQUAL_DOUBLE(45.0, document["Tmax"].as<double>());
  TEST_ASSERT_EQUAL_DOUBLE(22.0, document["Tmin"].as<double>());
}

void testOperationParserReadsEevMinimum()
{
  JsonDocument document;
  deserializeJson(document,
    "{\"eev_min_pulse_open\":\"45\",\"eev_max_pulse_open\":\"61\"}");

  OperationParseResult parsed = parseServerOperation(document.as<JsonObjectConst>());

  TEST_ASSERT_EQUAL_UINT16(0, parsed.invalidValues);
  TEST_ASSERT_TRUE(parsed.state.eevMinPulseOpen.present);
  TEST_ASSERT_EQUAL_DOUBLE(45, parsed.state.eevMinPulseOpen.value);
  TEST_ASSERT_TRUE(parsed.state.eevMaxPulseOpen.present);
  TEST_ASSERT_EQUAL_DOUBLE(61, parsed.state.eevMaxPulseOpen.value);
}

void testCopIsCompletedOnlyAfterHeatPumpStops()
{
  CopEstimator estimator;

  TEST_ASSERT_EQUAL_INT(static_cast<int>(CopCycleEvent::STARTED),
    static_cast<int>(estimator.update(true, 40.0, 30.0, 100.0, 10)));
  TEST_ASSERT_FALSE(estimator.estimate().valid);

  estimator.update(true, 38.0, 35.0, 800.0, 40);
  estimator.update(true, 45.0, 40.0, 1500.0, 80);
  TEST_ASSERT_FALSE(estimator.estimate().valid);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(CopCycleEvent::COMPLETED),
    static_cast<int>(estimator.update(false, 44.0, 41.0, 1600.0, 100)));

  const CopEstimate &estimate = estimator.estimate();
  TEST_ASSERT_TRUE(estimate.valid);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 38.0, estimate.startBottomTemperature);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 1.472, estimate.minimum);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 1.635, estimate.maximum);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 1.553, estimate.estimated);
}

void testCopBottomEstimateUsesOnlyStartupWindow()
{
  CopEstimator estimator;
  estimator.update(true, 40.0, 30.0, 0.0, 5);
  estimator.update(true, 36.0, 32.0, 200.0, 50);
  estimator.update(true, 30.0, 34.0, 400.0, 70);
  estimator.update(false, 42.0, 38.0, 1000.0, 100);

  TEST_ASSERT_DOUBLE_WITHIN(0.001, 36.0,
    estimator.estimate().startBottomTemperature);
}
}

int runAllTests()
{
  UNITY_BEGIN();
  RUN_TEST(testRepeatedOperationDoesNotScheduleCommandsAgain);
  RUN_TEST(testEmptyPatchDoesNotApplyDefaults);
  RUN_TEST(testOnlyChangedServerFieldIsScheduled);
  RUN_TEST(testOffHasPriorityAndIsNotRepeated);
  RUN_TEST(testAutoPvChangesForceOnlyAtThresholdTransitions);
  RUN_TEST(testPartialPatchPreservesPreviousServerValues);
  RUN_TEST(testRelaysFollowServerCoPumpState);
  RUN_TEST(testCwuModeDisablesLocalRelays);
  RUN_TEST(testLocalOffIsIndependentFromCloudWorkMode);
  RUN_TEST(testManualModeDoesNotSendOrApplyCloudCommands);
  RUN_TEST(testManualCwuDisablesRelaysWithoutSendingCommands);
  RUN_TEST(testInvalidTemperatureRangeIsIgnored);
  RUN_TEST(testRejectedQueueCommandIsRetried);
  RUN_TEST(testOperationParserAcceptsTypedValues);
  RUN_TEST(testOperationParserRejectsInvalidValues);
  RUN_TEST(testEevMaximumIsSentBeforeMinimum);
  RUN_TEST(testOperationParserReadsEevMinimum);
  RUN_TEST(testMaintenanceActionsAreSentOnceAndNotKept);
  RUN_TEST(testRestartResendsTheWholeStateAfterwards);
  RUN_TEST(testCoReportedOffIsSentAgain);
  RUN_TEST(testCloudOffKeepsCoOff);
  RUN_TEST(testForceIsSentAgainOnlyWhileIdle);
  RUN_TEST(testForceIsNotCheckedBeforeServerSendsIt);
  RUN_TEST(testLostHeatPumpGetsWholeStateBack);
  RUN_TEST(testLocalOffTurnsCoOffAgain);
  RUN_TEST(testManualModeIgnoresHeatPumpReport);
  RUN_TEST(testLostDeltaIsSentAgain);
  RUN_TEST(testLostSetpointIsSentAgainInCwuMode);
  RUN_TEST(testRoundedTemperaturesAreNotSentAgain);
  RUN_TEST(testDeltaChpcRejectsIsNotSentAgain);
  RUN_TEST(testTemperaturesAreNotCheckedInOffMode);
  RUN_TEST(testReportedTemperatureStringsAreParsed);
  RUN_TEST(testCopIsCompletedOnlyAfterHeatPumpStops);
  RUN_TEST(testCopBottomEstimateUsesOnlyStartupWindow);
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
