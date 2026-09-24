#include <heat_pump_data_processor.hpp>

#include <Arduino.h>

namespace {
double jsonDouble(JsonVariantConst value)
{
  if (value.isNull()) return 0.0;
  if (value.is<const char *>()) return String(value.as<const char *>()).toDouble();
  return value.as<double>();
}
}

bool HeatPumpDataProcessor::processFrame(const uint8_t *data, size_t length,
  HeatPumpDataUpdate &update)
{
  if (data == nullptr || length == 0) return false;

  update = {};
  DeserializationError error = deserializeJson(
    update.hp, reinterpret_cast<const char *>(data), length);
  if (error) return false;

  updateCop(update.hp.as<JsonObject>(), update);
  return true;
}

void HeatPumpDataProcessor::updateCop(JsonObject hp, HeatPumpDataUpdate &update)
{
  if (hp.isNull() || hp["HPS"].isNull() || hp["Tho"].isNull()
    || hp["Ttarget"].isNull()) return;

  const bool running = hp["HPS"].as<int>() > 0;
  const double topTemperature = jsonDouble(hp["Tho"]);
  const double middleTemperature = jsonDouble(hp["Ttarget"]);
  const double electricalEnergyWh = jsonDouble(hp["lt_pow"]);
  const uint32_t cycleDurationSeconds = static_cast<uint32_t>(
    jsonDouble(hp["lt_hp_on"]));

  CopCycleEvent event = copEstimator.update(running, topTemperature,
    middleTemperature, electricalEnergyWh, cycleDurationSeconds);

  if (event == CopCycleEvent::STARTED) {
    update.copState = CopDataState::STARTED;
    update.currentMiddleTemperature = middleTemperature;
    return;
  }

  if (copEstimator.cycleActive()) {
    update.copState = CopDataState::ACTIVE;
    update.currentMiddleTemperature = copEstimator.currentMiddleTemperature();
    return;
  }

  if (event != CopCycleEvent::COMPLETED) return;

  update.copState = CopDataState::COMPLETED;
  update.copEstimate = copEstimator.estimate();
}
