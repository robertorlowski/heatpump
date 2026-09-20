#pragma once

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <ArduinoJson.h>
#include <RTClib.h>
#include <domain_types.hpp>
#include "secrets.h"

const char devID = 0x10;
#define PV_DEVICE_ID 0x69

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
      return src["total_power"].is<int64_t>()
        && src["total_prod"].is<uint64_t>()
        && src["total_prod_today"].is<uint64_t>();
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

template <>
 struct Converter<HpPreferences> {
    static bool toJson(const HpPreferences& src, JsonVariant dst) {
      dst["work_mode"] = src.workMode;
      dst["co_min"] = src.coMin;
      dst["co_max"] = src.coMax;
      dst["cwu_min"] = src.cwuMin;
      dst["cwu_max"] = src.cwuMax;
      return true;
    }

    static bool checkJson(JsonVariantConst src) {
      return src.is<JsonObjectConst>();
    }
  };
}
