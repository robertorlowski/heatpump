#pragma once

#include <cstdint>

enum class CopCycleEvent : uint8_t {
  NONE,
  STARTED,
  COMPLETED,
};

struct CopEstimate {
  bool valid = false;
  double minimum = 0.0;
  double maximum = 0.0;
  double estimated = 0.0;
  double startMiddleTemperature = 0.0;
  double endMiddleTemperature = 0.0;
  double startBottomTemperature = 0.0;
};

class CopEstimator {
public:
  CopCycleEvent update(bool heatPumpRunning, double topTemperature,
    double middleTemperature, double electricalEnergyWh,
    uint32_t cycleDurationSeconds);

  bool cycleActive() const { return cycleActive_; }
  double currentMiddleTemperature() const { return endMiddleTemperature_; }
  const CopEstimate &estimate() const { return estimate_; }

private:
  void startCycle(double topTemperature, double middleTemperature,
    double electricalEnergyWh, uint32_t cycleDurationSeconds);
  void completeCycle(double topTemperature, double middleTemperature,
    double electricalEnergyWh);

  static constexpr double WATER_HEAT_CAPACITY_WH_PER_L_K = 1.163;
  static constexpr double TANK_VOLUME_LITERS = 300.0;
  static constexpr uint32_t BOTTOM_ESTIMATE_WINDOW_SECONDS = 60;

  bool wasRunning_ = false;
  bool cycleActive_ = false;
  double startTopTemperature_ = 0.0;
  double startMiddleTemperature_ = 0.0;
  double startBottomTemperature_ = 0.0;
  double endTopTemperature_ = 0.0;
  double endMiddleTemperature_ = 0.0;
  double electricalEnergyWh_ = 0.0;
  CopEstimate estimate_{};
};
