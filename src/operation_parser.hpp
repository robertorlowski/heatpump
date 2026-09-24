#pragma once

#include <ArduinoJson.h>
#include <operation_types.hpp>

struct OperationParseResult {
  ServerOperationState state;
  uint16_t invalidValues = 0;
};

OperationParseResult parseServerOperation(JsonObjectConst document);
