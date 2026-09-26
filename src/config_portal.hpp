#pragma once

#include <pv_telemetry.hpp>
#include <telemetry.hpp>

// Serves a single configuration page on port 80, reachable both on the local
// network and on the fallback access point.
void beginConfigPortal(const Telemetry &telemetry,
  const PvTelemetry &pvTelemetry);
void handleConfigPortal();
