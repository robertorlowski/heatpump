#pragma once

// Copy this file to secrets.h and provide local credentials. These values are
// only the defaults used until something is saved on the configuration page
// that the controller serves on port 80; a value stored in NVS wins.
#define WIFI_SSID "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"

// Identyfikator konfiguracji urządzenia z GET /api/devices w chpc-web.
#define CLOUD_ROOT_ID "your-device-root-id"
