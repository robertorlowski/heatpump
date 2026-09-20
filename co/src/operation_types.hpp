#pragma once

#include <domain_types.hpp>

template <typename T>
struct ServerValue {
  bool present = false;
  T value{};
};

struct ServerOperationState {
  ServerValue<WORK_MODE> workMode;
  ServerValue<double> coMin;
  ServerValue<double> coMax;
  ServerValue<double> cwuMin;
  ServerValue<double> cwuMax;
  ServerValue<bool> coPump;
  ServerValue<bool> sumpHeater;
  ServerValue<bool> coldPump;
  ServerValue<bool> hotPump;
  ServerValue<bool> force;
  ServerValue<double> workingWatt;
  ServerValue<double> eevMaxPulseOpen;
  ServerValue<double> eevSetpoint;
};

inline bool hasServerOperationValues(const ServerOperationState &state)
{
  return state.workMode.present
    || state.coMin.present
    || state.coMax.present
    || state.cwuMin.present
    || state.cwuMax.present
    || state.coPump.present
    || state.sumpHeater.present
    || state.coldPump.present
    || state.hotPump.present
    || state.force.present
    || state.workingWatt.present
    || state.eevMaxPulseOpen.present
    || state.eevSetpoint.present;
}

template <typename T>
inline void mergeServerValue(ServerValue<T> &target, const ServerValue<T> &patch)
{
  if (patch.present) target = patch;
}

inline void mergeServerOperation(
  ServerOperationState &target, const ServerOperationState &patch)
{
  mergeServerValue(target.workMode, patch.workMode);
  mergeServerValue(target.coMin, patch.coMin);
  mergeServerValue(target.coMax, patch.coMax);
  mergeServerValue(target.cwuMin, patch.cwuMin);
  mergeServerValue(target.cwuMax, patch.cwuMax);
  mergeServerValue(target.coPump, patch.coPump);
  mergeServerValue(target.sumpHeater, patch.sumpHeater);
  mergeServerValue(target.coldPump, patch.coldPump);
  mergeServerValue(target.hotPump, patch.hotPump);
  mergeServerValue(target.force, patch.force);
  mergeServerValue(target.workingWatt, patch.workingWatt);
  mergeServerValue(target.eevMaxPulseOpen, patch.eevMaxPulseOpen);
  mergeServerValue(target.eevSetpoint, patch.eevSetpoint);
}
