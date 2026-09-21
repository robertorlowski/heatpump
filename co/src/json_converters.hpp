#pragma once

#include <ArduinoJson.h>
#include <RTClib.h>

#include <domain_types.hpp>

namespace ArduinoJson {
template <>
struct Converter<PV> {
  static bool toJson(const PV &source, JsonVariant destination)
  {
    destination["total_power"] = source.total_power;
    destination["total_prod"] = source.total_prod;
    destination["total_prod_today"] = source.total_prod_today;
    destination["temperature"] = source.temperature;
    return true;
  }

  static PV fromJson(JsonVariantConst source)
  {
    PV value;
    value.total_power = source["total_power"];
    value.total_prod = source["total_prod"];
    value.total_prod_today = source["total_prod_today"];
    value.temperature = source["temperature"];
    return value;
  }

  static bool checkJson(JsonVariantConst source)
  {
    return source["total_power"].is<int64_t>()
      && source["total_prod"].is<uint64_t>()
      && source["total_prod_today"].is<uint64_t>();
  }
};

template <>
struct Converter<DateTime> {
  static bool toJson(const DateTime currentTime, JsonVariant destination)
  {
    char formattedTime[20];
    sprintf(formattedTime, "%04d.%02d.%02d %02d:%02d:%02d",
      currentTime.year(), currentTime.month(), currentTime.day(),
      currentTime.hour(), currentTime.minute(), currentTime.second());
    return destination.set(formattedTime);
  }

  static bool checkJson(JsonVariantConst)
  {
    return true;
  }
};

template <>
struct Converter<WORK_MODE> {
  static bool toJson(const WORK_MODE workMode, JsonVariant destination)
  {
    switch (workMode) {
      case MANUAL: return destination.set("M");
      case AUTO: return destination.set("A");
      case AUTO_PV: return destination.set("PV");
      case CWU: return destination.set("CWU");
      case OFF: return destination.set("OFF");
    }
    return destination.set("");
  }

  static bool checkJson(JsonVariantConst)
  {
    return true;
  }
};

template <>
struct Converter<ControllerMode> {
  static bool toJson(const ControllerMode mode, JsonVariant destination)
  {
    switch (mode) {
      case ControllerMode::OFF: return destination.set("OFF");
      case ControllerMode::CLOUD: return destination.set("CLOUD");
      case ControllerMode::MANUAL_CO: return destination.set("MANUAL_CO");
      case ControllerMode::MANUAL_CWU: return destination.set("MANUAL_CWU");
    }
    return destination.set("");
  }

  static bool checkJson(JsonVariantConst)
  {
    return true;
  }
};

template <>
struct Converter<DeviceSettings> {
  static bool toJson(const DeviceSettings &source, JsonVariant destination)
  {
    destination["work_mode"] = source.workMode;
    destination["co_min"] = source.coMin;
    destination["co_max"] = source.coMax;
    destination["cwu_min"] = source.cwuMin;
    destination["cwu_max"] = source.cwuMax;
    return true;
  }

  static bool checkJson(JsonVariantConst source)
  {
    return source.is<JsonObjectConst>();
  }
};
}
