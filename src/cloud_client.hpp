#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>

class CloudClient {
public:
  void begin();
  void tick();
  bool takeOperationRequest();
  String post(const String &path, const JsonDocument &data);
  int lastHttpStatus() const;
  uint32_t requestErrorCount() const;
  uint32_t webSocketDisconnectCount() const;

private:
  static CloudClient *instance;
  static void handleWebSocketEvent(
    WStype_t type, uint8_t *payload, size_t length);

  HTTPClient http;
  WebSocketsClient webSocket;
  bool operationRequested = false;
  unsigned long lastWifiReconnectAt = 0;
  int httpStatus = 0;
  uint32_t requestErrors = 0;
  uint32_t webSocketDisconnects = 0;
};
