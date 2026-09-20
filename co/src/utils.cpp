#include <utils.hpp>

#include <NTPClient.h>
#include <WiFi.h>
#include <Wire.h>

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

void displayRow(Adafruit_ST7735 &tft, int row, int column,
  const String &name, const String &value, const String &defaultValue = "")
{
  if (column == -1)
    tft.setCursor(7, row * 10 + 20);
  else
    tft.setCursor(column * 65, row * 10 + 20);

  tft.printf("%s%s", name.c_str(),
    (value != "" ? value : defaultValue).c_str());
}
}

void sendSerialText(const String &text)
{
  Serial.println(text);
  Serial.flush();
}

bool synchronizeRtc(RTC_DS3231 &rtc)
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

bool initialize(RTC_DS3231 &rtc, Adafruit_ST7735 &tft)
{
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);
  tft.setTextWrap(false);
  tft.fillScreen(ST77XX_BLACK);
  tft.invertDisplay(false);
  tft.setTextSize(1);

  PrintD(tft, "WIFI connecting...");
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long wifiStartedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStartedAt < 10000) {
    delay(50);
  }

  if (WiFi.status() != WL_CONNECTED) {
    PrintD(tft, "Error WIFI ");
    return false;
  }

  WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(),
    IPAddress(8, 8, 8, 8));
  PrintD(tft, "Connected.", 0);
  PrintD(tft, "IP: " + WiFi.localIP().toString(), 1);

  PrintD(tft, "Initialize RTC...");
  return synchronizeRtc(rtc);
}

void PrintD(Adafruit_ST7735 &tft, const String &text, int line)
{
  if (line == 0) tft.fillScreen(ST77XX_BLACK);
  int y = line == 0 ? 5 : (line * 2 * 9) + 2;
  tft.setCursor(0, y);
  tft.fillRect(0, y, 160, 11, ST7735_BLACK);
  tft.print(text);
}

String jsonAsString(JsonVariantConst json)
{
  if (json.isNull()) return "";
  if (json.is<const char *>()) return String(json.as<const char *>());
  if (json.is<bool>()) return json.as<bool>() ? "1" : "0";
  if (json.is<double>()) return String(json.as<double>());
  return "";
}

int jsonAsInt(JsonVariantConst json)
{
  return json.isNull() ? 0 : json.as<int>();
}

void PrintMode(Adafruit_ST7735 &tft, WORK_MODE work)
{
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW);
  tft.clearWriteError();

  switch (work) {
    case MANUAL:
      tft.setCursor(20, 70);
      tft.printf("MANUAL");
      break;
    case AUTO:
      tft.setCursor(20, 70);
      tft.printf("AUTO");
      break;
    case CWU:
      tft.setCursor(40, 70);
      tft.printf("CWU");
      break;
    case AUTO_PV:
      tft.setCursor(40, 70);
      tft.printf("PV");
      break;
    case OFF:
      tft.setCursor(30, 70);
      tft.printf("CO OFF");
      break;
  }

  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 150);
  if (WiFi.status() == WL_CONNECTED)
    tft.printf("IP: %s", WiFi.localIP().toString().c_str());
  else
    tft.printf("Error WIFI");
}

void digitalWriteA(Adafruit_ST7735 &tft, uint8_t pin, uint8_t value)
{
  uint8_t previous = digitalRead(pin);
  digitalWrite(pin, value);
  if (previous != value) {
    tft.initR(INITR_BLACKTAB);
    tft.setRotation(0);
    tft.setTextWrap(false);
  }
}

void PrintAll(Adafruit_ST7735 &tft, bool coOn, bool cwuOn,
  double cwuTemperature, const DateTime &rtcTime,
  const JsonDocument &hpDocument, WORK_MODE work, const PV &pv,
  const HpPreferences &prefs)
{
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(1);
  tft.clearWriteError();
  tft.setCursor(0, 3);
  tft.printf("%04d.%02d.%02d %02d:%02d", rtcTime.year(), rtcTime.month(),
    rtcTime.day(), rtcTime.hour(), rtcTime.minute());
  tft.setCursor(110, 3);

  switch (work) {
    case MANUAL: tft.printf("M"); break;
    case AUTO: tft.printf("A"); break;
    case AUTO_PV: tft.printf("PV"); break;
    case CWU: tft.printf("CWU"); break;
    case OFF: tft.printf("OFF"); break;
  }

  tft.drawLine(0, 23, 420, 23, ST77XX_BLUE);
  tft.setCursor(0, 13);
  tft.printf("P:%lld/%llu", static_cast<long long>(pv.total_power),
    static_cast<unsigned long long>(pv.total_prod_today));
  tft.setCursor(90, 13);
  tft.printf("T:%2.0f", pv.temperature);

  JsonObjectConst hp = hpDocument["HP"].as<JsonObjectConst>();
  if (hp.isNull()) return;

  tft.setTextSize(2);
  if (hp["F"]) {
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(0, 30);
    tft.printf("F");
    tft.setTextColor(ST77XX_WHITE);
  }

  if (hp["CO"].isNull()) {
    displayRow(tft, 1, -1, "  T:", "----");
  } else {
    if (coOn) tft.setTextColor(ST77XX_RED);
    displayRow(tft, 1, -1, "  T:", jsonAsString(hp["Ttarget"]));
    tft.setTextColor(ST77XX_WHITE);
  }

  if (cwuOn) tft.setTextColor(ST77XX_RED);
  if (cwuTemperature > 0) {
    char temperature[10];
    sprintf(temperature, "%2.1f", cwuTemperature);
    displayRow(tft, 3, -1, "CWU:", temperature);
  }

  tft.setTextColor(ST77XX_WHITE);
  tft.drawLine(0, 70, 420, 70, ST77XX_BLUE);
  tft.setTextSize(1);

  int row = 6;
  displayRow(tft, row++, 0, "   T.HP:",
    jsonAsString(hp["Tmin"]) + "/" + jsonAsString(hp["Tmax"]));
  displayRow(tft, row++, 0, "   T.CO:",
    String(prefs.coMin, 0) + "/" + String(prefs.coMax, 0));
  displayRow(tft, row++, 0, "  T.CWU:",
    String(prefs.cwuMin, 0) + "/" + String(prefs.cwuMax, 0));

  displayRow(tft, row, 0, "T.be:", jsonAsString(hp["Tbe"]));
  displayRow(tft, row++, 1, "T.ae:", jsonAsString(hp["Tae"]));
  displayRow(tft, row, 0, "T.hp:", jsonAsString(hp["Tsump"]));
  displayRow(tft, row++, 1, "T.ho:", jsonAsString(hp["Tho"]));
  displayRow(tft, row, 0, "E.ev:", jsonAsString(hp["EEV"]));
  displayRow(tft, row++, 1, "E.dt:", jsonAsString(hp["EEV_dt"]));
  displayRow(tft, row, 0, "E.ps:", jsonAsString(hp["EEV_pos"]));
  displayRow(tft, row++, 1, "Watt:", jsonAsString(hp["Watts"]));
  displayRow(tft, row, 0, "HC.s:",
    hp["HCS"].isNull() ? "" : hp["HCS"] ? "ON" : "OFF");
  displayRow(tft, row++, 1, "CC.s:",
    hp["CCS"].isNull() ? "" : hp["CCS"] ? "ON" : "OFF");
}
