#pragma once

#include <Adafruit_ST7735.h>
#include <ArduinoJson.h>
#include <RTClib.h>

#include <domain_types.hpp>

bool initializeDevice(RTC_DS3231 &rtc, Adafruit_ST7735 &display);
bool synchronizeClock(RTC_DS3231 &rtc);
void displayStatus(Adafruit_ST7735 &display, const String &text, int line = 0);
void displayControllerMode(Adafruit_ST7735 &display,
  ControllerMode controllerMode, WORK_MODE workMode);
void renderDashboard(Adafruit_ST7735 &display, bool coOn,
  const DateTime &rtcTime, const JsonDocument &telemetry,
  ControllerMode controllerMode, WORK_MODE workMode, const PV &pv,
  bool pvTemperatureCurrent, const DeviceSettings &settings);
void writeRelayOutput(Adafruit_ST7735 &display, uint8_t pin, uint8_t value);
void writeSerialResponse(const String &text);
