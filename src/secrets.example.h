#pragma once

// Copy this file to secrets.h and provide local credentials. These values are
// only the defaults used until something is saved on the configuration page
// that the controller serves on port 80; a value stored in NVS wins.
#define WIFI_SSID "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"

// Opcjonalne. Bez tej wartości sterownik sam rejestruje się w chpc-web
// (POST /api/devices/register, deviceId = SN) i zapisuje otrzymany rootId w NVS.
// #define CLOUD_ROOT_ID "existing-device-root-id"
