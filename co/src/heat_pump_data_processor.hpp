#pragma once

#include <ArduinoJson.h>

#include <cop_estimator.hpp>

enum class CopDataState : uint8_t {
  UNCHANGED,
  STARTED,
  ACTIVE,
  COMPLETED,
};

struct HeatPumpDataUpdate {
  JsonDocument hp;
  CopDataState copState = CopDataState::UNCHANGED;
  double currentMiddleTemperature = 0.0;
  CopEstimate copEstimate{};
};

class HeatPumpDataProcessor {
public:
  bool processFrame(const uint8_t *data, size_t length,
    HeatPumpDataUpdate &update);

private:
  CopEstimator copEstimator;

  void updateCop(JsonObject hp, HeatPumpDataUpdate &update);
};
