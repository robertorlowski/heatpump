#pragma once

#include <cstdint>

#include <hardware_config.hpp>

// One entry per microinverter port, in the order the DTU reports them.
struct PvPanel {
  char inverter_serial[13] = {};  // 12 BCD digits taken from the DTU record
  uint8_t port = 0;               // port number inside the microinverter
  int32_t power = 0;              // W
  uint32_t prod_today = 0;        // Wh
  uint32_t prod_total = 0;        // Wh
  float temperature = 0.0f;       // degrees Celsius
};

struct PV {
  int64_t total_power = 0;
  uint64_t total_prod = 0;
  uint64_t total_prod_today = 0;
  float temperature = 0.0f;
  bool pv_power = false;
  uint8_t panel_count = 0;
  PvPanel panels[PV_MAX_PANELS]{};
};

enum SERIAL_OPERATION {
  GET_HP_DATA,
  GET_PV_DATA_1,
  GET_PV_DATA_2,
  SET_HP_FORCE_ON,
  SET_HP_FORCE_OFF,
  SET_HP_CO_ON,
  SET_HP_CO_OFF,
  SET_SUMP_HEATER_ON,
  SET_SUMP_HEATER_OFF,
  SET_COLD_PUMP_ON,
  SET_COLD_PUMP_OFF,
  SET_HOT_PUMP_ON,
  SET_HOT_PUMP_OFF,
  SET_T_SETPOINT_CO,
  SET_T_DELTA_CO,
  SET_EEV_MAXPULSES_OPEN,
  SET_EEV_MINWORKPOS,
  HP_ERROR_RESET,
  HP_RESTART,
  SET_WORKING_WATT,
  SET_EEV_SETPOINT,
};

enum WORK_MODE : int16_t {
  MANUAL,
  AUTO,
  AUTO_PV,
  CWU,
  OFF
};

enum class ControllerMode : uint8_t {
  OFF,
  CLOUD,
  MANUAL_CO,
  MANUAL_CWU,
};

struct DeviceSettings {
  WORK_MODE workMode = WORK_MODE::OFF;
  double coMin = 35.0;
  double coMax = 45.0;
  double cwuMin = 40.0;
  double cwuMax = 47.0;
  ControllerMode controllerMode = ControllerMode::CLOUD;
};
