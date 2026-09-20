#include "cop_estimator.hpp"

#include <algorithm>
#include <cmath>

constexpr double CopEstimator::WATER_HEAT_CAPACITY_WH_PER_L_K;
constexpr double CopEstimator::TANK_VOLUME_LITERS;
constexpr uint32_t CopEstimator::BOTTOM_ESTIMATE_WINDOW_SECONDS;

namespace {
double tankAverageTemperature(double top, double middle, double bottom)
{
  // Trapezoidal integration over two equal-height halves of the tank.
  return (top + 2.0 * middle + bottom) / 4.0;
}
}

CopCycleEvent CopEstimator::update(bool heatPumpRunning,
  double topTemperature, double middleTemperature,
  double electricalEnergyWh, uint32_t cycleDurationSeconds)
{
  if (!std::isfinite(topTemperature) || !std::isfinite(middleTemperature)) {
    return CopCycleEvent::NONE;
  }

  if (heatPumpRunning) {
    const bool started = !wasRunning_ || !cycleActive_;
    if (started) {
      startCycle(topTemperature, middleTemperature, electricalEnergyWh,
        cycleDurationSeconds);
    } else {
      endTopTemperature_ = topTemperature;
      endMiddleTemperature_ = middleTemperature;
      electricalEnergyWh_ = std::max(electricalEnergyWh_, electricalEnergyWh);

      if (cycleDurationSeconds <= BOTTOM_ESTIMATE_WINDOW_SECONDS) {
        startBottomTemperature_ = std::min(startBottomTemperature_, topTemperature);
      }
    }

    wasRunning_ = true;
    return started ? CopCycleEvent::STARTED : CopCycleEvent::NONE;
  }

  wasRunning_ = false;
  if (!cycleActive_) return CopCycleEvent::NONE;

  completeCycle(topTemperature, middleTemperature, electricalEnergyWh);
  cycleActive_ = false;
  return CopCycleEvent::COMPLETED;
}

void CopEstimator::startCycle(double topTemperature, double middleTemperature,
  double electricalEnergyWh, uint32_t cycleDurationSeconds)
{
  cycleActive_ = true;
  startTopTemperature_ = topTemperature;
  startMiddleTemperature_ = middleTemperature;
  startBottomTemperature_ = topTemperature;
  endTopTemperature_ = topTemperature;
  endMiddleTemperature_ = middleTemperature;
  electricalEnergyWh_ = std::max(0.0, electricalEnergyWh);
  estimate_ = {};

  // If the first observation is already outside the startup window, THO is
  // still the only available approximation of the initial bottom temperature.
  (void)cycleDurationSeconds;
}

void CopEstimator::completeCycle(double topTemperature,
  double middleTemperature, double electricalEnergyWh)
{
  // The last running sample protects the endpoint against cooling immediately
  // after the compressor stops.
  endTopTemperature_ = std::max(endTopTemperature_, topTemperature);
  endMiddleTemperature_ = std::max(endMiddleTemperature_, middleTemperature);
  electricalEnergyWh_ = std::max(electricalEnergyWh_, electricalEnergyWh);

  const double startAverage = tankAverageTemperature(startTopTemperature_,
    startMiddleTemperature_, startBottomTemperature_);

  // Lower bound: the bottom layer did not warm up during the cycle.
  const double endAverageMinimum = tankAverageTemperature(endTopTemperature_,
    endMiddleTemperature_, startBottomTemperature_);

  // Upper bound: the bottom layer reached the middle sensor temperature.
  const double endBottomMaximum = std::max(startBottomTemperature_,
    endMiddleTemperature_);
  const double endAverageMaximum = tankAverageTemperature(endTopTemperature_,
    endMiddleTemperature_, endBottomMaximum);

  const double minimumDelta = std::max(0.0, endAverageMinimum - startAverage);
  const double maximumDelta = std::max(minimumDelta,
    endAverageMaximum - startAverage);

  estimate_ = {};
  estimate_.startMiddleTemperature = startMiddleTemperature_;
  estimate_.endMiddleTemperature = endMiddleTemperature_;
  estimate_.startBottomTemperature = startBottomTemperature_;

  if (!std::isfinite(electricalEnergyWh_) || electricalEnergyWh_ <= 0.0
    || maximumDelta <= 0.0) {
    return;
  }

  const double minimumHeatWh = WATER_HEAT_CAPACITY_WH_PER_L_K
    * TANK_VOLUME_LITERS * minimumDelta;
  const double maximumHeatWh = WATER_HEAT_CAPACITY_WH_PER_L_K
    * TANK_VOLUME_LITERS * maximumDelta;

  estimate_.minimum = minimumHeatWh / electricalEnergyWh_;
  estimate_.maximum = maximumHeatWh / electricalEnergyWh_;
  estimate_.estimated = (estimate_.minimum + estimate_.maximum) / 2.0;
  estimate_.valid = std::isfinite(estimate_.estimated);
}
