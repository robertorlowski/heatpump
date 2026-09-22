#include <cloud_client.hpp>

#include <WiFi.h>
#include <device_config.hpp>

namespace {
constexpr const char *CLOUD_HOST = "chpc-web.onrender.com";
constexpr const char *CLOUD_BASE_URL = "https://chpc-web.onrender.com/api/";

// The main loop is blocked for the whole request, so the timeouts must cover
// a TLS handshake plus a slow response without stalling the pump bus for
// minutes. A cold-started instance misses one cycle and is picked up by the
// next one.
constexpr int32_t CONNECT_TIMEOUT_MS = 5000;
constexpr uint16_t RESPONSE_TIMEOUT_MS = 5000;

String deviceQuery()
{
  return String("rootId=") + deviceConfig().rootId;
}

String cloudUrl(const String &normalizedPath)
{
  const char separator = normalizedPath.indexOf('?') >= 0 ? '&' : '?';
  return String(CLOUD_BASE_URL) + normalizedPath + separator + deviceQuery();
}
}

CloudClient *CloudClient::instance = nullptr;

void CloudClient::begin()
{
  instance = this;
  String webSocketPath = String("/ws?") + deviceQuery();
  webSocket.beginSSL(CLOUD_HOST, 443, webSocketPath.c_str());
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

  if (!http.begin(cloudUrl(normalizedPath))) {
    httpStatus = 0;
    requestErrors++;
    return "";
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Cache-Control", "no-cache");
  http.setConnectTimeout(CONNECT_TIMEOUT_MS);
  http.setTimeout(RESPONSE_TIMEOUT_MS);

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
    {
      JsonDocument message;
      DeserializationError error = deserializeJson(message, payload, length);
      if (!error
        && message["type"] == "operation"
        && message["rootId"] == deviceConfig().rootId.c_str())
        instance->operationRequested = true;
      break;
    }
    case WStype_DISCONNECTED:
      instance->webSocketDisconnects++;
      break;
    default:
      break;
  }
}
