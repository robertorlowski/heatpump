#include <operation_controller.hpp>

OperationController::OperationController(CommandSink &commands, long pvForceThreshold)
  : commands(commands), pvForceThreshold(pvForceThreshold)
{
}

void OperationController::applyServerPatch(const ServerOperationState &patch)
{
  // Maintenance actions run in every controller mode and are not kept: the
  // server sends each one once.
  if (patch.errorReset.present && patch.errorReset.value)
    commands.enqueuePriority(SERIAL_OPERATION::HP_ERROR_RESET);
  if (patch.restart.present && patch.restart.value) {
    commands.enqueuePriority(SERIAL_OPERATION::HP_RESTART);
    // CHPC forgets its non-persistent state (forced pumps, force start), so
    // everything is sent again with the next server operation.
    resetScheduledState();
  }

  if (localMode != ControllerMode::CLOUD) return;

  ServerOperationState accepted = patch;
  accepted.errorReset = {};
  accepted.restart = {};
  if (!hasServerOperationValues(accepted)) return;


  DeviceSettings nextPreferences = prefs;
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
  cloudStateReady = true;

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
      if (cloudStateReady) reconcile();
      break;

    case ControllerMode::MANUAL_CO:
      setRelayState(true, true);
      break;

    case ControllerMode::MANUAL_CWU:
      setRelayState(false, false);
      break;
  }
}

void OperationController::updatePv(const PV &newPv)
{
  pv = newPv;
  if (localMode == ControllerMode::CLOUD
    && cloudStateReady && prefs.workMode == WORK_MODE::AUTO_PV) reconcile();
}

void OperationController::updateHeatPumpReport(const HeatPumpReport &report)
{
  bool resync = resyncPending;
  resyncPending = false;

  if (localMode == ControllerMode::OFF) {
    // CHPC keeps CO in EEPROM, so it can come back on from the pump itself.
    if (resync || report.coOn || report.force) scheduleOffSequence();
    return;
  }
  if (localMode != ControllerMode::CLOUD || !cloudStateReady) return;

  if (resync) {
    resetScheduledState();
  } else {
    WORK_MODE mode = prefs.workMode;
    if (report.coOn != (mode != WORK_MODE::OFF)) lastHpCo = {};

    // CHPC accepts force only while idle and clears it on every stop.
    bool force;
    if (!report.running && wantedForce(mode, force) && report.force != force)
      lastScheduled.force = {};
  }
  reconcile();
}

void OperationController::heatPumpLost()
{
  resyncPending = true;
}

void OperationController::tick()
{
  if (!retryPending) return;
  if (localMode == ControllerMode::CLOUD && cloudStateReady) reconcile();
  if (localMode == ControllerMode::OFF) {
    retryPending = false;
    scheduleOffSequence();
  }
}

ControllerMode OperationController::controllerMode() const
{
  return localMode;
}

const DeviceSettings &OperationController::preferences() const
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

// False when nothing decides force yet: the server has not sent it.
bool OperationController::wantedForce(WORK_MODE mode, bool &force) const
{
  if (mode == WORK_MODE::OFF) {
    force = false;
    return true;
  }
  if (mode == WORK_MODE::AUTO_PV) {
    force = pv.pv_power && pv.total_power >= pvForceThreshold;
    return true;
  }
  if (!desired.force.present) return false;
  force = desired.force.value;
  return true;
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
  // Both local relays are driven as one. The server's co_pomp only matters
  // while the work mode actually heats CO; otherwise the pair stays off.
  const bool enabled = isCoMode(mode)
    && (!desired.coPump.present || desired.coPump.value);

  setRelayState(enabled, enabled);
}

void OperationController::reconcile()
{
  if (localMode != ControllerMode::CLOUD || !cloudStateReady) return;
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

    bool force;
    if (wantedForce(mode, force))
      scheduleBool(lastScheduled.force, force,
        SERIAL_OPERATION::SET_HP_FORCE_ON, SERIAL_OPERATION::SET_HP_FORCE_OFF);
  }

  if (desired.workingWatt.present)
    scheduleDouble(lastScheduled.workingWatt, desired.workingWatt.value,
      SERIAL_OPERATION::SET_WORKING_WATT);

  // Maximum before minimum. CHPC moves the other limit when a new one would
  // cross it, so this order ends with the requested pair either way.
  if (desired.eevMaxPulseOpen.present)
    scheduleDouble(lastScheduled.eevMaxPulseOpen, desired.eevMaxPulseOpen.value,
      SERIAL_OPERATION::SET_EEV_MAXPULSES_OPEN);

  if (desired.eevMinPulseOpen.present)
    scheduleDouble(lastScheduled.eevMinPulseOpen, desired.eevMinPulseOpen.value,
      SERIAL_OPERATION::SET_EEV_MINWORKPOS);

  if (desired.eevSetpoint.present)
    scheduleDouble(lastScheduled.eevSetpoint, desired.eevSetpoint.value,
      SERIAL_OPERATION::SET_EEV_SETPOINT);

  updateRelayState(mode);
}
