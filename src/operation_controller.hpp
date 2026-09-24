#pragma once

#include <command_sink.hpp>
#include <operation_types.hpp>

class OperationController {
public:
  explicit OperationController(CommandSink &commands, long pvForceThreshold = 2000);

  void applyServerPatch(const ServerOperationState &patch);
  void setControllerMode(ControllerMode mode);
  void updatePv(const PV &pv);
  void tick();

  ControllerMode controllerMode() const;
  const DeviceSettings &preferences() const;
  const ServerOperationState &serverState() const;
  bool coRelay() const;
  bool cwuRelay() const;
  bool takeRelayChanged();
  bool takeModeChanged();
  uint32_t preferenceValidationErrorCount() const;

private:
  CommandSink &commands;
  long pvForceThreshold;
  DeviceSettings prefs;
  ServerOperationState desired;
  ServerOperationState lastScheduled;
  ServerValue<bool> lastHpCo;
  ServerValue<double> lastSetpoint;
  ServerValue<double> lastDelta;
  PV pv;
  ControllerMode localMode = ControllerMode::CLOUD;
  bool coRelayState = false;
  bool cwuRelayState = false;
  bool relayChanged = false;
  bool modeChanged = false;
  bool retryPending = false;
  bool cloudStateReady = false;
  uint32_t preferenceValidationErrors = 0;

  void reconcile();
  void scheduleOffSequence();
  void resetScheduledState();
  void setRelayState(bool coEnabled, bool cwuEnabled);
  void updateRelayState(WORK_MODE mode);
  bool isCoMode(WORK_MODE mode) const;
  bool scheduleBool(ServerValue<bool> &last, bool value,
    SERIAL_OPERATION onOperation, SERIAL_OPERATION offOperation,
    bool always = false, bool priority = false);
  bool scheduleDouble(ServerValue<double> &last, double value,
    SERIAL_OPERATION operation);
};
