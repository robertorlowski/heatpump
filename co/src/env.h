#pragma once

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <ArduinoJson.h>



const char devID = 0x10;
#define PV_DEVICE_ID 0x69


#define BD

// ESP32-WROOM
#define TFT_DC 12   // A0
#define TFT_CS 13   // CS
#define TFT_MOSI 14 // SDA
#define TFT_CLK 27  // SCK
#define TFT_RST 0
#define TFT_MISO 0

// #define RELAY_CWU 16
#define RELAY_HP_CWU 25
#define RELAY_HP_CO 26
#define PWR 18
#define SWITCH_POMP_CO 5

#ifdef BD
#define SSID "Jagodzianka"
#define PASSWORD "Jagoda25"
#else
#define SSID "luxtech"
#define PASSWORD "RT1900ac"
#endif

typedef struct
{
  uint8_t hour;
  uint8_t minute;
} ScheduleTime;

typedef struct
{
  ScheduleTime slotStart;
  ScheduleTime slotStop;
} ScheduleSlot;

typedef struct
{
    long total_power = 0;
    long total_prod = 0;
    long total_prod_today = 0; 
    float temperature = 0.0;
    bool pv_power = false;
} PV;

enum SERIAL_OPERATION { 
  GET_HP_DATA,
  GET_PV_DATA_1, 
  GET_PV_DATA_2, 
  SET_HP_FORCE_ON, 
  SET_HP_FORCE_OFF,
  SET_HP_CO_ON,
  SET_HP_CO_OFF,
  SET_HP_CWU_ON,
  SET_HP_CWU_OFF,
  SET_SUMP_HEATER_ON,
  SET_SUMP_HEATER_OFF,
  SET_COLD_POMP_ON,
  SET_COLD_POMP_OFF,
  SET_HOT_POMP_ON,
  SET_HOT_POMP_OFF,
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

ScheduleSlot coSlots[]{
    {{23, 00}, {4, 0}},
    {{13, 0}, {14, 30}}
};

ScheduleSlot cwuSlots[] {
    {{13, 30}, {14, 45}},
    // {{22, 0}, {23, 0}},
    {{4, 30}, {5, 30}}
};

ScheduleSlot updateManualMode {
  {23, 00}, {23, 5}
};

namespace ArduinoJson {

  template <>
  struct Converter<PV> {
    static bool toJson(const PV& src, JsonVariant dst) {
      dst["total_power"] = src.total_power;
      dst["total_prod"] = src.total_prod;
      dst["total_prod_today"] = src.total_prod_today;
      dst["temperature"] = src.temperature;
      return true;
    }

    static PV fromJson(JsonVariantConst src) {
      PV _t;
      _t.total_power = src["total_power"];
      _t.total_prod = src["total_prod"];
      _t.total_prod_today =src["total_prod_today"];
      _t.temperature =src["temperature"]; 
      return _t;
    }

    static bool checkJson(JsonVariantConst src) {
      return src["total_power"].is<uint8_t>() && src["total_prod"].is<uint8_t>() && src["total_prod_today"].is<uint8_t>();
    }
  };

  template <>
  struct Converter<ScheduleSlot> {
    static bool toJson(const ScheduleSlot& src, JsonVariant dst) {
      JsonDocument slot;
      slot["slot_start_hour"] = src.slotStart.hour;
      slot["slot_start_minute"] = src.slotStart.minute;
      slot["slot_stop_hour"] = src.slotStop.hour;
      slot["slot_stop_minute"] = src.slotStop.minute;
      return dst.set(slot);
    }

    static ScheduleSlot fromJson(JsonVariantConst src) {
      ScheduleSlot _s;
      _s.slotStart.hour = src["slot_start_hour"];
      _s.slotStart.minute = src["slot_start_minute"];
      _s.slotStop.hour = src["slot_stop_hour"];
      _s.slotStop.minute = src["slot_stop_minute"];
      return _s;
    }

    static bool checkJson(JsonVariantConst src) {
      return src["slot_start_hour"].is<uint8_t>() && src["slot_start_minute"].is<uint8_t>() && 
        src["slot_stop_hour"].is<uint8_t>() && src["slot_stop_minute"].is<uint8_t>();
    }
  };


template <>
 struct Converter<DateTime> {
    static bool toJson(const DateTime currentTime, JsonVariant dst) {
      char s_time[20];
      sprintf(s_time, "%04d.%02d.%02d %02d:%02d:%02d", currentTime.year(), currentTime.month(), 
        currentTime.day(), currentTime.hour(), currentTime.minute(), currentTime.second());
      return dst.set(s_time);
    }

    static bool checkJson(JsonVariantConst src) {
      return true;
    }
  };

template <>
 struct Converter<WORK_MODE> {
    static bool toJson(const WORK_MODE workMode, JsonVariant dst) {
      switch (workMode)
      {
        case MANUAL:
          return dst.set("M");
        case AUTO:
          return dst.set("A");
        case AUTO_PV:
          return dst.set("PV");
        case CWU:
          return dst.set("CWU");
        case OFF:
          return dst.set("OFF");
      }
      return dst.set("");
    }

    static bool checkJson(JsonVariantConst src) {
      return true;
    }
  };
}