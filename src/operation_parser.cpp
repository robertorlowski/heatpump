#include <operation_parser.hpp>

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {
bool textEqualsIgnoreCase(const char *text, const char *expected)
{
  if (text == nullptr) return false;
  while (std::isspace(static_cast<unsigned char>(*text))) text++;

  while (*expected != '\0') {
    if (std::tolower(static_cast<unsigned char>(*text))
      != std::tolower(static_cast<unsigned char>(*expected))) return false;
    text++;
    expected++;
  }

  while (std::isspace(static_cast<unsigned char>(*text))) text++;
  return *text == '\0';
}

bool parseBoolean(JsonVariantConst variant, bool &result)
{
  if (variant.is<bool>()) {
    result = variant.as<bool>();
    return true;
  }

  if (variant.is<int64_t>() || variant.is<uint64_t>() || variant.is<double>()) {
    double number = variant.as<double>();
    if (number != 0.0 && number != 1.0) return false;
    result = number == 1.0;
    return true;
  }

  if (!variant.is<const char *>()) return false;
  const char *text = variant.as<const char *>();
  if (textEqualsIgnoreCase(text, "1") || textEqualsIgnoreCase(text, "true")) {
    result = true;
    return true;
  }
  if (textEqualsIgnoreCase(text, "0") || textEqualsIgnoreCase(text, "false")) {
    result = false;
    return true;
  }
  return false;
}

bool parseNumber(JsonVariantConst variant, double minimum, double maximum,
  bool roundToWhole, double &result)
{
  double value;
  if (variant.is<bool>()) {
    return false;
  } else if (variant.is<int64_t>() || variant.is<uint64_t>()
    || variant.is<double>()) {
    value = variant.as<double>();
  } else if (variant.is<const char *>()) {
    const char *text = variant.as<const char *>();
    if (text == nullptr) return false;

    char *end = nullptr;
    value = strtod(text, &end);
    if (end == text) return false;
    while (std::isspace(static_cast<unsigned char>(*end))) end++;
    if (*end != '\0') return false;
  } else {
    return false;
  }

  if (!std::isfinite(value) || value < minimum || value > maximum) return false;
  result = roundToWhole ? round(value) : value;
  return true;
}

void readBoolean(JsonObjectConst document, const char *key,
  ServerValue<bool> &target, uint16_t &invalidValues)
{
  JsonVariantConst variant = document[key];
  if (variant.isNull()) return;
  if (!parseBoolean(variant, target.value)) {
    invalidValues++;
    return;
  }
  target.present = true;
}

bool parseWorkMode(const char *text, WORK_MODE &result)
{
  if (text == nullptr) return false;
  if (strcmp(text, "M") == 0) { result = WORK_MODE::MANUAL; return true; }
  if (strcmp(text, "A") == 0) { result = WORK_MODE::AUTO; return true; }
  if (strcmp(text, "PV") == 0) { result = WORK_MODE::AUTO_PV; return true; }
  if (strcmp(text, "CWU") == 0) { result = WORK_MODE::CWU; return true; }
  if (strcmp(text, "OFF") == 0) { result = WORK_MODE::OFF; return true; }
  return false;
}

void readWorkMode(JsonObjectConst document, const char *key,
  ServerValue<WORK_MODE> &target, uint16_t &invalidValues)
{
  JsonVariantConst variant = document[key];
  if (variant.isNull()) return;
  if (!variant.is<const char *>()
    || !parseWorkMode(variant.as<const char *>(), target.value)) {
    invalidValues++;
    return;
  }
  target.present = true;
}

void readNumber(JsonObjectConst document, const char *key,
  ServerValue<double> &target, double minimum, double maximum,
  bool roundToWhole, uint16_t &invalidValues)
{
  JsonVariantConst variant = document[key];
  if (variant.isNull()) return;
  if (!parseNumber(variant, minimum, maximum, roundToWhole, target.value)) {
    invalidValues++;
    return;
  }
  target.present = true;
}
}

OperationParseResult parseServerOperation(JsonObjectConst document)
{
  OperationParseResult result;
  if (document.isNull()) return result;

  readWorkMode(document, "work_mode", result.state.workMode,
    result.invalidValues);
  readNumber(document, "co_min", result.state.coMin, 1, 50, true,
    result.invalidValues);
  readNumber(document, "co_max", result.state.coMax, 1, 50, true,
    result.invalidValues);
  readNumber(document, "cwu_min", result.state.cwuMin, 1, 50, true,
    result.invalidValues);
  readNumber(document, "cwu_max", result.state.cwuMax, 1, 50, true,
    result.invalidValues);
  readBoolean(document, "co_pomp", result.state.coPump, result.invalidValues);
  readBoolean(document, "sump_heater", result.state.sumpHeater,
    result.invalidValues);
  readBoolean(document, "cold_pomp", result.state.coldPump,
    result.invalidValues);
  readBoolean(document, "hot_pomp", result.state.hotPump,
    result.invalidValues);
  readBoolean(document, "force", result.state.force, result.invalidValues);
  readBoolean(document, "error_reset", result.state.errorReset,
    result.invalidValues);
  readBoolean(document, "restart", result.state.restart, result.invalidValues);
  readNumber(document, "working_watt", result.state.workingWatt, 0, 25599,
    true, result.invalidValues);
  readNumber(document, "eev_max_pulse_open", result.state.eevMaxPulseOpen,
    0, 255, true, result.invalidValues);
  readNumber(document, "eev_min_pulse_open", result.state.eevMinPulseOpen,
    0, 255, true, result.invalidValues);
  readNumber(document, "eev_setpoint", result.state.eevSetpoint, 0, 255.99,
    false, result.invalidValues);

  return result;
}
