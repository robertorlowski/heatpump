#pragma once

#include <cstdint>

struct PV {
  int64_t total_power = 0;
  uint64_t total_prod = 0;
  uint64_t total_prod_today = 0;
  float temperature = 0.0f;
  bool pv_power = false;
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
  char wifiSsid[64] = {};
  char wifiPassword[64] = {};
  char rootId[64] = {};
};
