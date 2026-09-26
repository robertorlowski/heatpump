#pragma once

#include <ArduinoJson.h>
#include <RTClib.h>

#include <domain_types.hpp>

// PV reading sent to POST pv/add, separately from the heat pump telemetry:
// the installation summary plus every microinverter port.
class PvTelemetry {
public:
  void update(const DateTime &time, const PV &pv);

  bool hasReading() const;
  const JsonDocument &document() const;

private:
  JsonDocument data;
};
