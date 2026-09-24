#include <device_config.hpp>

#include <Preferences.h>

#include "secrets.h"

#ifndef WIFI_SSID
#error "WIFI_SSID must be defined in secrets.h"
#endif
#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD must be defined in secrets.h"
#endif
#ifndef CLOUD_ROOT_ID
#error "CLOUD_ROOT_ID must be defined in secrets.h"
#endif

namespace {
constexpr const char *KEY_WIFI_SSID = "wifi_ssid";
constexpr const char *KEY_WIFI_PASSWORD = "wifi_pass";
constexpr const char *KEY_ROOT_ID = "root_id";

DeviceConfig config;

String storedOrDefault(Preferences &preferences, const char *key,
  const char *fallback)
{
  String stored = preferences.getString(key, "");
  return stored.length() > 0 ? stored : String(fallback);
}
}

void loadDeviceConfig()
{
  Preferences preferences;
  preferences.begin(PREFERENCES_NAMESPACE, true);
  config.wifiSsid = storedOrDefault(preferences, KEY_WIFI_SSID, WIFI_SSID);
  config.wifiPassword =
    storedOrDefault(preferences, KEY_WIFI_PASSWORD, WIFI_PASSWORD);
  config.rootId = storedOrDefault(preferences, KEY_ROOT_ID, CLOUD_ROOT_ID);
  preferences.end();
}

const DeviceConfig &deviceConfig()
{
  return config;
}

bool saveDeviceConfig(const DeviceConfig &next)
{
  if (next.wifiSsid.length() == 0 || next.rootId.length() == 0) return false;

  Preferences preferences;
  if (!preferences.begin(PREFERENCES_NAMESPACE, false)) return false;

  // putString returns the number of bytes written, which is legitimately zero
  // for the empty password of an open network, so only the fields that must
  // not be empty are checked.
  bool stored = preferences.putString(KEY_WIFI_SSID, next.wifiSsid) > 0;
  preferences.putString(KEY_WIFI_PASSWORD, next.wifiPassword);
  stored = preferences.putString(KEY_ROOT_ID, next.rootId) > 0 && stored;
  preferences.end();

  if (stored) config = next;
  return stored;
}
