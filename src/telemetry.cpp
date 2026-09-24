#include <telemetry.hpp>

#include <cmath>

#include <json_converters.hpp>

Telemetry::Telemetry()
{
  data["HP"].to<JsonObject>();
  data["PV"].to<JsonObject>();
}

void Telemetry::updateSnapshot(const DateTime &time, bool coPump, bool cwuPump,
  const PV &pv, ControllerMode controllerMode,
  const DeviceSettings &settings)
{
  data["time"] = time;
  data["co_pomp"] = coPump;
  data["cwu_pomp"] = cwuPump;
  data["pv_power"] = pv.pv_power;
  data["controller_mode"] = controllerMode;
  data["work_mode"] = settings.workMode;
  data["co_min"] = settings.coMin;
  data["co_max"] = settings.coMax;
  data["cwu_min"] = settings.cwuMin;
  data["cwu_max"] = settings.cwuMax;
}

void Telemetry::updateSerialDiagnostics(uint32_t queueOverflow,
  uint32_t readTimeout, uint32_t receiveOverflow, uint32_t pvCrcError,
  uint32_t hpJsonError, uint32_t pvFrameError)
{
  data["serial_queue_overflow"] = queueOverflow;
  data["serial_read_timeout"] = readTimeout;
  data["serial_receive_overflow"] = receiveOverflow;
  data["pv_crc_error"] = pvCrcError;
  data["hp_json_error"] = hpJsonError;
  data["pv_frame_error"] = pvFrameError;
}

void Telemetry::updateCloudDiagnostics(int httpStatus, uint32_t requestError,
  uint32_t webSocketDisconnect, uint32_t responseParseError)
{
  data["cloud_http_status"] = httpStatus;
  data["cloud_request_error"] = requestError;
  data["websocket_disconnect"] = webSocketDisconnect;
  data["cloud_response_parse_error"] = responseParseError;
}

void Telemetry::updateOperationDiagnostics(
  uint32_t operationValidationError, uint32_t preferenceValidationError)
{
  data["operation_validation_error"] = operationValidationError;
  data["preference_validation_error"] = preferenceValidationError;
}

void Telemetry::updatePv(const PV &pv)
{
  data["pv_power"] = pv.pv_power;
  data["PV"] = pv;
}

void Telemetry::updateHeatPump(const HeatPumpDataUpdate &update)
{
  data["HP"] = update.hp;

  switch (update.copState) {
    case CopDataState::STARTED:
      data["t_min"] = update.currentMiddleTemperature;
      data["t_max"] = update.currentMiddleTemperature;
      data.remove("cop");
      data.remove("cop_min");
      data.remove("cop_max");
      data.remove("cop_bottom_start");
      break;

    case CopDataState::ACTIVE:
      data["t_max"] = update.currentMiddleTemperature;
      break;

    case CopDataState::COMPLETED: {
      const CopEstimate &estimate = update.copEstimate;
      data["t_min"] = estimate.startMiddleTemperature;
      data["t_max"] = estimate.endMiddleTemperature;
      data["cop_bottom_start"] = estimate.startBottomTemperature;
      if (!estimate.valid) {
        data.remove("cop");
        data.remove("cop_min");
        data.remove("cop_max");
        break;
      }
      data["cop_min"] = std::round(estimate.minimum * 100.0) / 100.0;
      data["cop_max"] = std::round(estimate.maximum * 100.0) / 100.0;
      data["cop"] = std::round(estimate.estimated * 100.0) / 100.0;
      break;
    }

    case CopDataState::UNCHANGED:
      break;
  }
}

void Telemetry::updateControllerState(bool coPump, bool cwuPump,
  ControllerMode controllerMode, const DeviceSettings &settings)
{
  data["co_pomp"] = coPump;
  data["cwu_pomp"] = cwuPump;
  data["controller_mode"] = controllerMode;
  data["work_mode"] = settings.workMode;
}

bool Telemetry::heatPumpRunning() const
{
  return data["HP"]["HPS"].as<int>() > 0;
}

const JsonDocument &Telemetry::document() const
{
  return data;
}
