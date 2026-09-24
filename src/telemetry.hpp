#pragma once

#include <ArduinoJson.h>
#include <RTClib.h>

#include <domain_types.hpp>
#include <heat_pump_data_processor.hpp>

class Telemetry {
public:
  Telemetry();

  void updateSnapshot(const DateTime &time, bool coPump, bool cwuPump,
    const PV &pv, ControllerMode controllerMode,
    const DeviceSettings &settings);
  void updateSerialDiagnostics(uint32_t queueOverflow, uint32_t readTimeout,
    uint32_t receiveOverflow, uint32_t pvCrcError, uint32_t hpJsonError,
    uint32_t pvFrameError);
  void updateCloudDiagnostics(int httpStatus, uint32_t requestError,
    uint32_t webSocketDisconnect, uint32_t responseParseError);
  void updateOperationDiagnostics(uint32_t operationValidationError,
    uint32_t preferenceValidationError);
  void updatePv(const PV &pv);
  void updateHeatPump(const HeatPumpDataUpdate &update);
  void updateControllerState(bool coPump, bool cwuPump,
    ControllerMode controllerMode, const DeviceSettings &settings);

  bool heatPumpRunning() const;
  const JsonDocument &document() const;

private:
  JsonDocument data;
};
