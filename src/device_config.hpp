#pragma once

#include <Arduino.h>

// Runtime overrides for the compile-time defaults in secrets.h. A value stored
// in NVS wins; an empty or missing one falls back to the built-in default, so
// a freshly flashed controller works without visiting the configuration page.
// An empty rootId means the controller is not registered in the cloud yet and
// obtains one from POST /api/devices/register.
struct DeviceConfig {
  String wifiSsid;
  String wifiPassword;
  String rootId;
};

constexpr const char *PREFERENCES_NAMESPACE = "hp";

// Network the controller opens when it cannot join the configured one, so a
// wrong password never makes the configuration page unreachable.
constexpr const char *CONFIG_AP_SSID = "HP-CO-setup";

// Basic-auth credentials guarding /install. The telemetry page on / is open.
// These live in tracked source, so treat them as a lock on the front door,
// not as a secret.
constexpr const char *INSTALL_USER = "admin";
constexpr const char *INSTALL_PASSWORD = "123!";

void loadDeviceConfig();
const DeviceConfig &deviceConfig();
bool deviceRegistered();

// Factory MAC burnt into eFuse, as 12 upper-case hex digits in the order
// WiFi.macAddress() prints them. It identifies the controller in the cloud.
const String &deviceSerial();

// Stores the Wi-Fi fields only; the rootId is owned by the registration.
bool saveWifiConfig(const String &ssid, const String &password);
bool saveRootId(const String &rootId);
// Forgets a rootId the server does not match with this serial, so the
// controller registers again.
void clearRootId();
