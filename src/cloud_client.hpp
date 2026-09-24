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
  // True while the controller has no rootId and the retry delay has passed.
  // registerDevice() blocks for a whole HTTP request, so the caller runs it
  // only when the serial bus is idle.
  bool registrationDue() const;
  void registerDevice();
  String post(const String &path, const JsonDocument &data);
  int lastHttpStatus() const;
  uint32_t requestErrorCount() const;
  uint32_t webSocketDisconnectCount() const;

private:
  static CloudClient *instance;
  static void handleWebSocketEvent(
    WStype_t type, uint8_t *payload, size_t length);

  void startWebSocket();
  String send(const String &url, const JsonDocument &data);

  HTTPClient http;
  WebSocketsClient webSocket;
  bool webSocketStarted = false;
  bool operationRequested = false;
  bool registrationAttempted = false;
  unsigned long lastRegistrationAt = 0;
  unsigned long lastWifiReconnectAt = 0;
  int httpStatus = 0;
  uint32_t requestErrors = 0;
  uint32_t webSocketDisconnects = 0;
};
