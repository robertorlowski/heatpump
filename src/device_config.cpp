#include <device_config.hpp>

#include <Preferences.h>
#include <esp_mac.h>

#include "secrets.h"

#ifndef WIFI_SSID
#error "WIFI_SSID must be defined in secrets.h"
#endif
#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD must be defined in secrets.h"
#endif
// Optional: a controller flashed without it registers itself in the cloud.
#ifndef CLOUD_ROOT_ID
#define CLOUD_ROOT_ID ""
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

String readSerial()
{
  uint8_t mac[6] = {};
  if (esp_efuse_mac_get_default(mac) != ESP_OK) return "";

  char serial[13];
  snprintf(serial, sizeof(serial), "%02X%02X%02X%02X%02X%02X",
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return serial;
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

bool deviceRegistered()
{
  return config.rootId.length() > 0;
}

const String &deviceSerial()
{
  static const String serial = readSerial();
  return serial;
}

bool saveWifiConfig(const String &ssid, const String &password)
{
  if (ssid.length() == 0) return false;

  Preferences preferences;
  if (!preferences.begin(PREFERENCES_NAMESPACE, false)) return false;

  // putString returns the number of bytes written, which is legitimately zero
  // for the empty password of an open network, so only the SSID is checked.
  bool stored = preferences.putString(KEY_WIFI_SSID, ssid) > 0;
  preferences.putString(KEY_WIFI_PASSWORD, password);
  preferences.end();

  if (stored) {
    config.wifiSsid = ssid;
    config.wifiPassword = password;
  }
  return stored;
}

bool saveRootId(const String &rootId)
{
  if (rootId.length() == 0) return false;

  Preferences preferences;
  if (!preferences.begin(PREFERENCES_NAMESPACE, false)) return false;
  bool stored = preferences.putString(KEY_ROOT_ID, rootId) > 0;
  preferences.end();

  if (stored) config.rootId = rootId;
  return stored;
}

void clearRootId()
{
  Preferences preferences;
  if (preferences.begin(PREFERENCES_NAMESPACE, false)) {
    preferences.remove(KEY_ROOT_ID);
    preferences.end();
  }
  // Also drops a CLOUD_ROOT_ID default until the next restart; if that one
  // is wrong too, the server rejects it again and the cycle repeats once.
  config.rootId = "";
}
