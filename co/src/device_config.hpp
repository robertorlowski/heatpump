#pragma once

#include <Arduino.h>

// Runtime overrides for the compile-time defaults in secrets.h. A value stored
// in NVS wins; an empty or missing one falls back to the built-in default, so
// a freshly flashed controller works without visiting the configuration page.
struct DeviceConfig {
  String wifiSsid;
  String wifiPassword;
  String rootId;
};

constexpr const char *PREFERENCES_NAMESPACE = "hp";

// Network the controller opens when it cannot join the configured one, so a
// wrong password never makes the configuration page unreachable.
constexpr const char *CONFIG_AP_SSID = "HP-CO-setup";

void loadDeviceConfig();
const DeviceConfig &deviceConfig();
bool saveDeviceConfig(const DeviceConfig &config);
