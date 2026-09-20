#include <operation_controller.hpp>

OperationController::OperationController(CommandSink &commands, long pvForceThreshold)
  : commands(commands), pvForceThreshold(pvForceThreshold)
{
}

void OperationController::applyServerPatch(const ServerOperationState &patch)
{
  if (localMode != ControllerMode::CLOUD) return;
  if (!hasServerOperationValues(patch)) return;

  ServerOperationState accepted = patch;

  HpPreferences nextPreferences = prefs;
  if (accepted.workMode.present) nextPreferences.workMode = accepted.workMode.value;
  if (accepted.coMin.present) nextPreferences.coMin = accepted.coMin.value;
  if (accepted.coMax.present) nextPreferences.coMax = accepted.coMax.value;
  if (accepted.cwuMin.present) nextPreferences.cwuMin = accepted.cwuMin.value;
  if (accepted.cwuMax.present) nextPreferences.cwuMax = accepted.cwuMax.value;

  if (nextPreferences.coMin > nextPreferences.coMax) {
    preferenceValidationErrors++;
    if (accepted.coMin.present) accepted.coMin.present = false;
    if (accepted.coMax.present) accepted.coMax.present = false;
    nextPreferences.coMin = prefs.coMin;
    nextPreferences.coMax = prefs.coMax;
  }

  if (nextPreferences.cwuMin > nextPreferences.cwuMax) {
    preferenceValidationErrors++;
    if (accepted.cwuMin.present) accepted.cwuMin.present = false;
    if (accepted.cwuMax.present) accepted.cwuMax.present = false;
    nextPreferences.cwuMin = prefs.cwuMin;
    nextPreferences.cwuMax = prefs.cwuMax;
  }

  if (!hasServerOperationValues(accepted)) return;

  bool serverModeChanged = accepted.workMode.present
    && (!desired.workMode.present || desired.workMode.value != accepted.workMode.value);

  mergeServerOperation(desired, accepted);
  prefs = nextPreferences;

  if (serverModeChanged) {
    modeChanged = true;
    if (prefs.workMode == WORK_MODE::OFF) scheduleOffSequence();
  }

  reconcile();
}

void OperationController::setControllerMode(ControllerMode mode)
{
  if (localMode == mode) return;

  localMode = mode;
  modeChanged = true;
  retryPending = false;

  switch (localMode) {
    case ControllerMode::OFF:
      setRelayState(false, false);
      scheduleOffSequence();
      break;

    case ControllerMode::CLOUD:
      resetScheduledState();
      reconcile();
      break;

    case ControllerMode::MANUAL_CO:
      setRelayState(true, false);
      break;

    case ControllerMode::MANUAL_CWU:
      setRelayState(false, true);
      break;
  }
}

void OperationController::updatePv(const PV &newPv)
{
  pv = newPv;
  if (localMode == ControllerMode::CLOUD
    && prefs.workMode == WORK_MODE::AUTO_PV) reconcile();
}

void OperationController::tick()
{
  if (!retryPending) return;
  if (localMode == ControllerMode::CLOUD) reconcile();
  if (localMode == ControllerMode::OFF) {
    retryPending = false;
    scheduleOffSequence();
  }
}

ControllerMode OperationController::controllerMode() const
{
  return localMode;
}

const HpPreferences &OperationController::preferences() const
{
  return prefs;
}

const ServerOperationState &OperationController::serverState() const
{
  return desired;
}

bool OperationController::coRelay() const
{
  return coRelayState;
}

bool OperationController::cwuRelay() const
{
  return cwuRelayState;
}

bool OperationController::takeRelayChanged()
{
  bool changed = relayChanged;
  relayChanged = false;
  return changed;
}

bool OperationController::takeModeChanged()
{
  bool changed = modeChanged;
  modeChanged = false;
  return changed;
}

uint32_t OperationController::preferenceValidationErrorCount() const
{
  return preferenceValidationErrors;
}

bool OperationController::isCoMode(WORK_MODE mode) const
{
  return mode == WORK_MODE::MANUAL
    || mode == WORK_MODE::AUTO
    || mode == WORK_MODE::AUTO_PV;
}

bool OperationController::scheduleBool(ServerValue<bool> &last, bool value,
  SERIAL_OPERATION onOperation, SERIAL_OPERATION offOperation,
  bool always, bool priority)
{
  if (!always && last.present && last.value == value) return true;
  bool queued = priority
    ? commands.enqueuePriority(value ? onOperation : offOperation)
    : commands.enqueue(value ? onOperation : offOperation);
  if (!queued) {
    retryPending = true;
    return false;
  }
  last.present = true;
  last.value = value;
  return true;
}

bool OperationController::scheduleDouble(ServerValue<double> &last, double value,
  SERIAL_OPERATION operation)
{
  if (last.present && last.value == value) return true;
  if (!commands.enqueue(operation, value)) {
    retryPending = true;
    return false;
  }
  last.present = true;
  last.value = value;
  return true;
}

void OperationController::scheduleOffSequence()
{
  scheduleBool(lastHpCo, false,
    SERIAL_OPERATION::SET_HP_CO_ON, SERIAL_OPERATION::SET_HP_CO_OFF, true, true);
  scheduleBool(lastScheduled.force, false,
    SERIAL_OPERATION::SET_HP_FORCE_ON, SERIAL_OPERATION::SET_HP_FORCE_OFF, true, true);
  scheduleBool(lastScheduled.hotPump, false,
    SERIAL_OPERATION::SET_HOT_PUMP_ON, SERIAL_OPERATION::SET_HOT_PUMP_OFF, true, true);
  scheduleBool(lastScheduled.coldPump, false,
    SERIAL_OPERATION::SET_COLD_PUMP_ON, SERIAL_OPERATION::SET_COLD_PUMP_OFF, true, true);
}

void OperationController::resetScheduledState()
{
  lastScheduled = {};
  lastHpCo = {};
  lastSetpoint = {};
  lastDelta = {};
}

void OperationController::setRelayState(bool coEnabled, bool cwuEnabled)
{
  if (coRelayState == coEnabled && cwuRelayState == cwuEnabled) return;
  coRelayState = coEnabled;
  cwuRelayState = cwuEnabled;
  relayChanged = true;
}

void OperationController::updateRelayState(WORK_MODE mode)
{
  bool coMode = isCoMode(mode);
  bool requestedCoRelay = !coMode
    ? false
    : (desired.coPump.present ? desired.coPump.value : coMode);
  bool requestedCwuRelay = requestedCoRelay;

  setRelayState(requestedCoRelay, requestedCwuRelay);
}

void OperationController::reconcile()
{
  if (localMode != ControllerMode::CLOUD) return;
  retryPending = false;
  WORK_MODE mode = prefs.workMode;
  bool coMode = isCoMode(mode);

  scheduleBool(lastHpCo, mode != WORK_MODE::OFF,
    SERIAL_OPERATION::SET_HP_CO_ON, SERIAL_OPERATION::SET_HP_CO_OFF,
    false, mode == WORK_MODE::OFF);

  if (mode != WORK_MODE::OFF) {
    double minimum = coMode ? prefs.coMin : prefs.cwuMin;
    double maximum = coMode ? prefs.coMax : prefs.cwuMax;
    scheduleDouble(lastSetpoint, maximum, SERIAL_OPERATION::SET_T_SETPOINT_CO);
    scheduleDouble(lastDelta, maximum - minimum, SERIAL_OPERATION::SET_T_DELTA_CO);
  }

  if (desired.sumpHeater.present)
    scheduleBool(lastScheduled.sumpHeater, desired.sumpHeater.value,
      SERIAL_OPERATION::SET_SUMP_HEATER_ON, SERIAL_OPERATION::SET_SUMP_HEATER_OFF);

  if (mode == WORK_MODE::OFF) {
    scheduleBool(lastScheduled.coldPump, false,
      SERIAL_OPERATION::SET_COLD_PUMP_ON, SERIAL_OPERATION::SET_COLD_PUMP_OFF,
      false, true);
  } else if (desired.coldPump.present) {
    scheduleBool(lastScheduled.coldPump, desired.coldPump.value,
      SERIAL_OPERATION::SET_COLD_PUMP_ON, SERIAL_OPERATION::SET_COLD_PUMP_OFF);
  }

  if (mode == WORK_MODE::OFF) {
    scheduleBool(lastScheduled.hotPump, false,
      SERIAL_OPERATION::SET_HOT_PUMP_ON, SERIAL_OPERATION::SET_HOT_PUMP_OFF,
      false, true);
    scheduleBool(lastScheduled.force, false,
      SERIAL_OPERATION::SET_HP_FORCE_ON, SERIAL_OPERATION::SET_HP_FORCE_OFF,
      false, true);
  } else {
    if (desired.hotPump.present)
      scheduleBool(lastScheduled.hotPump, desired.hotPump.value,
        SERIAL_OPERATION::SET_HOT_PUMP_ON, SERIAL_OPERATION::SET_HOT_PUMP_OFF);

    if (mode == WORK_MODE::AUTO_PV) {
      bool force = pv.pv_power && pv.total_power >= pvForceThreshold;
      scheduleBool(lastScheduled.force, force,
        SERIAL_OPERATION::SET_HP_FORCE_ON, SERIAL_OPERATION::SET_HP_FORCE_OFF);
    } else if (desired.force.present) {
      scheduleBool(lastScheduled.force, desired.force.value,
        SERIAL_OPERATION::SET_HP_FORCE_ON, SERIAL_OPERATION::SET_HP_FORCE_OFF);
    }
  }

  if (desired.workingWatt.present)
    scheduleDouble(lastScheduled.workingWatt, desired.workingWatt.value,
      SERIAL_OPERATION::SET_WORKING_WATT);

  if (desired.eevMaxPulseOpen.present)
    scheduleDouble(lastScheduled.eevMaxPulseOpen, desired.eevMaxPulseOpen.value,
      SERIAL_OPERATION::SET_EEV_MAXPULSES_OPEN);

  if (desired.eevSetpoint.present)
    scheduleDouble(lastScheduled.eevSetpoint, desired.eevSetpoint.value,
      SERIAL_OPERATION::SET_EEV_SETPOINT);

  updateRelayState(mode);
}
