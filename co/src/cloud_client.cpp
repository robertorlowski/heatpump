#include <cloud_client.hpp>

#include <WiFi.h>

namespace {
constexpr const char *CLOUD_HOST = "chpc-web.onrender.com";
constexpr const char *CLOUD_BASE_URL = "https://chpc-web.onrender.com/api/";
}

CloudClient *CloudClient::instance = nullptr;

void CloudClient::begin()
{
  instance = this;
  webSocket.beginSSL(CLOUD_HOST, 443, "/ws");
  webSocket.onEvent(handleWebSocketEvent);
  webSocket.setReconnectInterval(10000);
}

void CloudClient::tick()
{
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long now = millis();
    if (now - lastWifiReconnectAt >= 10000) {
      lastWifiReconnectAt = now;
      WiFi.reconnect();
    }
    return;
  }
  webSocket.loop();
}

bool CloudClient::takeOperationRequest()
{
  bool requested = operationRequested;
  operationRequested = false;
  return requested;
}

String CloudClient::post(const String &path, const JsonDocument &data)
{
  if (WiFi.status() != WL_CONNECTED) {
    httpStatus = 0;
    requestErrors++;
    return "";
  }

  String normalizedPath = path;
  while (normalizedPath.startsWith("/")) normalizedPath.remove(0, 1);

  if (!http.begin(String(CLOUD_BASE_URL) + normalizedPath)) {
    httpStatus = 0;
    requestErrors++;
    return "";
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Cache-Control", "no-cache");
  http.setTimeout(1000);

  String payload;
  serializeJson(data, payload);
  httpStatus = http.POST(payload);

  String response;
  if (httpStatus >= 200 && httpStatus < 300) {
    response = http.getString();
  } else {
    requestErrors++;
  }
  http.end();
  return response;
}

int CloudClient::lastHttpStatus() const
{
  return httpStatus;
}

uint32_t CloudClient::requestErrorCount() const
{
  return requestErrors;
}

uint32_t CloudClient::webSocketDisconnectCount() const
{
  return webSocketDisconnects;
}

void CloudClient::handleWebSocketEvent(
  WStype_t type, uint8_t *payload, size_t length)
{
  if (instance == nullptr) return;

  switch (type) {
    case WStype_CONNECTED:
      instance->webSocket.sendTXT("ESP32");
      break;
    case WStype_TEXT:
      if (length == 9 && memcmp(payload, "operation", 9) == 0)
        instance->operationRequested = true;
      break;
    case WStype_DISCONNECTED:
      instance->webSocketDisconnects++;
      break;
    default:
      break;
  }
}
