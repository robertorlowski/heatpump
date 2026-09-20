#pragma once

#include <ArduinoJson.h>
#include <RTClib.h>
#include <env.h>

constexpr size_t SERIAL_BUFFER = 2048;

bool initialize(RTC_DS3231 &rtc, Adafruit_ST7735 &tft);
bool synchronizeRtc(RTC_DS3231 &rtc);
void PrintD(Adafruit_ST7735 &tft, const String &text, int line = 0);
void PrintMode(Adafruit_ST7735 &tft, WORK_MODE work);
void PrintAll(Adafruit_ST7735 &tft, bool coOn, bool cwuOn, double cwuTemperature,
  const DateTime &rtcTime, const JsonDocument &hpDocument, WORK_MODE work,
  const PV &pv, const HpPreferences &prefs);
void digitalWriteA(Adafruit_ST7735 &tft, uint8_t pin, uint8_t value);

void sendSerialText(const String &text);
String jsonAsString(JsonVariantConst json);
int jsonAsInt(JsonVariantConst json);
