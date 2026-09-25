#include <device_io.hpp>

#include <NTPClient.h>
#include <WiFi.h>
#include <Wire.h>

#include <device_config.hpp>

namespace {
uint8_t lastSundayOfMonth(uint16_t year, uint8_t month)
{
  uint16_t nextYear = month == 12 ? year + 1 : year;
  uint8_t nextMonth = month == 12 ? 1 : month + 1;
  DateTime lastDay = DateTime(nextYear, nextMonth, 1) - TimeSpan(1, 0, 0, 0);
  return lastDay.day() - lastDay.dayOfTheWeek();
}

long warsawUtcOffset(unsigned long utcEpoch)
{
  DateTime utc(utcEpoch);
  uint16_t year = utc.year();
  DateTime dstStart(year, 3, lastSundayOfMonth(year, 3), 1, 0, 0);
  DateTime dstEnd(year, 10, lastSundayOfMonth(year, 10), 1, 0, 0);
  return utcEpoch >= dstStart.unixtime() && utcEpoch < dstEnd.unixtime()
    ? 7200L : 3600L;
}

void displayRow(Adafruit_ST7735 &display, int row, int column,
  const String &name, const String &value, const String &defaultValue = "")
{
  if (column == -1)
    display.setCursor(7, row * 10 + 20);
  else
    display.setCursor(column * 65, row * 10 + 20);

  display.printf("%s%s", name.c_str(),
    (value != "" ? value : defaultValue).c_str());
}

void printCentered(Adafruit_ST7735 &display, const char *text, int16_t y)
{
  int16_t boundsX, boundsY;
  uint16_t width, height;
  display.getTextBounds(text, 0, y, &boundsX, &boundsY, &width, &height);
  display.setCursor((display.width() - width) / 2, y);
  display.print(text);
}

String jsonValueToString(JsonVariantConst value)
{
  if (value.isNull()) return "";
  if (value.is<const char *>()) return String(value.as<const char *>());
  if (value.is<bool>()) return value.as<bool>() ? "1" : "0";
  if (value.is<double>()) return String(value.as<double>());
  return "";
}
}

void writeSerialResponse(const String &text)
{
  Serial.println(text);
  Serial.flush();
}

bool synchronizeClock(RTC_DS3231 &rtc)
{
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiUDP ntpService;
  NTPClient timeClient(ntpService, "pl.pool.ntp.org");
  timeClient.begin();
  timeClient.setTimeOffset(0);
  bool timeUpdated = timeClient.forceUpdate();
  unsigned long unixEpoch = timeClient.getEpochTime();
  timeClient.end();

  if (!timeUpdated || unixEpoch == 0) return false;
  rtc.adjust(DateTime(unixEpoch + warsawUtcOffset(unixEpoch)));
  return true;
}

bool initializeDevice(RTC_DS3231 &rtc, Adafruit_ST7735 &display)
{
  display.initR(INITR_BLACKTAB);
  display.setRotation(0);
  display.setTextWrap(false);
  display.fillScreen(ST77XX_BLACK);
  display.invertDisplay(false);
  display.setTextSize(1);

  const DeviceConfig &config = deviceConfig();
  displayStatus(display, "WIFI connecting...");

  // The controller always runs its own open network, so the configuration
  // page stays reachable no matter what happens to the configured Wi-Fi.
  // softAP() enables the access point on top of the station mode.
  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.softAP(CONFIG_AP_SSID);
  WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());
  unsigned long wifiStartedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStartedAt < 10000) {
    delay(50);
  }

  if (WiFi.status() != WL_CONNECTED) {
    displayStatus(display, "Error WIFI", 0);
    displayStatus(display, "AP: " + String(CONFIG_AP_SSID), 1);
    displayStatus(display, "IP: " + WiFi.softAPIP().toString(), 2);
    return false;
  }

  WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(),
    IPAddress(8, 8, 8, 8));
  displayStatus(display, "Connected.", 0);
  displayStatus(display, "IP: " + WiFi.localIP().toString(), 1);
  displayStatus(display, "AP: " + WiFi.softAPIP().toString(), 2);

  displayStatus(display, "Initialize RTC...");
  return synchronizeClock(rtc);
}

void displayStatus(Adafruit_ST7735 &display, const String &text, int line)
{
  if (line == 0) display.fillScreen(ST77XX_BLACK);
  int y = line == 0 ? 5 : (line * 2 * 9) + 2;
  display.setCursor(0, y);
  display.fillRect(0, y, 160, 11, ST7735_BLACK);
  display.print(text);
}

void displayControllerMode(Adafruit_ST7735 &display,
  ControllerMode controllerMode, WORK_MODE workMode)
{
  display.fillScreen(ST77XX_BLACK);
  display.setTextSize(2);
  display.setTextColor(ST77XX_YELLOW);
  display.clearWriteError();

  const char *source = "CLOUD";
  const char *mode = "";
  if (controllerMode == ControllerMode::OFF) {
    source = "LOCAL";
    mode = "OFF";
  } else if (controllerMode == ControllerMode::MANUAL_CO) {
    source = "MANUAL";
    mode = "CO";
  } else if (controllerMode == ControllerMode::MANUAL_CWU) {
    source = "MANUAL";
    mode = "CWU";
  } else {
    switch (workMode) {
      case MANUAL: mode = "MANUAL"; break;
      case AUTO: mode = "AUTO"; break;
      case CWU: mode = "CWU"; break;
      case AUTO_PV: mode = "AUTO PV"; break;
      case OFF: mode = "OFF"; break;
    }
  }

  printCentered(display, source, 60);
  printCentered(display, mode, 82);

  display.setTextColor(ST77XX_WHITE);
  display.setTextSize(1);
  display.setCursor(10, 140);
  if (WiFi.status() == WL_CONNECTED)
    display.printf("IP: %s", WiFi.localIP().toString().c_str());
  else
    display.printf("Error WIFI");

  // The access point is always up, so its address is the way back to the
  // configuration page exactly when the configured network is unavailable.
  display.setCursor(10, 150);
  display.printf("AP: %s", WiFi.softAPIP().toString().c_str());
}

void writeRelayOutput(Adafruit_ST7735 &display, uint8_t pin, uint8_t value)
{
  uint8_t previous = digitalRead(pin);
  digitalWrite(pin, value);
  if (previous != value) {
    display.initR(INITR_BLACKTAB);
    display.setRotation(0);
    display.setTextWrap(false);
  }
}

void renderDashboard(Adafruit_ST7735 &display, bool coOn,
  const DateTime &rtcTime, const JsonDocument &telemetry,
  ControllerMode controllerMode, WORK_MODE workMode, const PV &pv,
  const DeviceSettings &settings)
{
  display.fillScreen(ST77XX_BLACK);
  display.setTextSize(1);
  display.clearWriteError();
  display.setCursor(0, 3);
  display.printf("%04d.%02d.%02d %02d:%02d", rtcTime.year(), rtcTime.month(),
    rtcTime.day(), rtcTime.hour(), rtcTime.minute());
  display.setCursor(110, 3);

  if (controllerMode == ControllerMode::OFF) {
    display.printf("L-OFF");
  } else if (controllerMode == ControllerMode::MANUAL_CO) {
    display.printf("M-CO");
  } else if (controllerMode == ControllerMode::MANUAL_CWU) {
    display.printf("M-CWU");
  } else {
    switch (workMode) {
      case MANUAL: display.printf("C-M"); break;
      case AUTO: display.printf("C-A"); break;
      case AUTO_PV: display.printf("C-PV"); break;
      case CWU: display.printf("C-CWU"); break;
      case OFF: display.printf("C-OFF"); break;
    }
  }

  display.drawLine(0, 23, 420, 23, ST77XX_BLUE);
  display.setCursor(0, 13);
  display.printf("P:%lld/%llu", static_cast<long long>(pv.total_power),
    static_cast<unsigned long long>(pv.total_prod_today));
  display.setCursor(90, 13);
  display.printf("T:%2.0f", pv.temperature);

  JsonObjectConst hp = telemetry["HP"].as<JsonObjectConst>();
  if (hp.isNull()) return;

  display.setTextSize(2);
  if (hp["F"]) {
    display.setTextColor(ST77XX_YELLOW);
    display.setCursor(0, 30);
    display.printf("F");
    display.setTextColor(ST77XX_WHITE);
  }

  if (hp["CO"].isNull()) {
    displayRow(display, 1, -1, "  T:", "----");
  } else {
    if (coOn) display.setTextColor(ST77XX_RED);
    displayRow(display, 1, -1, "  T:", jsonValueToString(hp["Ttarget"]));
    display.setTextColor(ST77XX_WHITE);
  }

  display.setTextColor(ST77XX_WHITE);
  display.drawLine(0, 70, 420, 70, ST77XX_BLUE);
  display.setTextSize(1);

  int row = 6;
  displayRow(display, row++, 0, "   T.HP:",
    jsonValueToString(hp["Tmin"]) + "/" + jsonValueToString(hp["Tmax"]));
  displayRow(display, row++, 0, "   T.CO:",
    String(settings.coMin, 0) + "/" + String(settings.coMax, 0));
  displayRow(display, row++, 0, "  T.CWU:",
    String(settings.cwuMin, 0) + "/" + String(settings.cwuMax, 0));

  displayRow(display, row, 0, "T.be:", jsonValueToString(hp["Tbe"]));
  displayRow(display, row++, 1, "T.ae:", jsonValueToString(hp["Tae"]));
  displayRow(display, row, 0, "T.hp:", jsonValueToString(hp["Tsump"]));
  displayRow(display, row++, 1, "T.ho:", jsonValueToString(hp["Tho"]));
  displayRow(display, row, 0, "E.ev:", jsonValueToString(hp["EEV"]));
  displayRow(display, row++, 1, "E.dt:", jsonValueToString(hp["EEV_dt"]));
  displayRow(display, row, 0, "E.ps:", jsonValueToString(hp["EEV_pos"]));
  displayRow(display, row++, 1, "Watt:", jsonValueToString(hp["Watts"]));
  displayRow(display, row, 0, "HC.s:",
    hp["HCS"].isNull() ? "" : hp["HCS"] ? "ON" : "OFF");
  displayRow(display, row++, 1, "CC.s:",
    hp["CCS"].isNull() ? "" : hp["CCS"] ? "ON" : "OFF");
}
