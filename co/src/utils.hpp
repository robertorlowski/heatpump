#include <RTClib.h>
#include <Wire.h>
#include "WiFi.h"
#include <NTPClient.h>
#include <env.h>

const int SERIAL_BUFFER = 1024;

bool checkSchedule(DateTime currentTime, ScheduleSlot slot);
void initialize(RTC_DS3231 rtc, Adafruit_ST7735 tft);

// print lcd
void PrintD(Adafruit_ST7735 tft, String str, int line = 0);
void PrintAll(Adafruit_ST7735 tft, bool co_on, DateTime rtcTime, JsonDocument hpDoc, PV pv);

// serial
void writeSerial(const uint8_t *buffer, uint8_t length);
void printSerial(String str);
int getDataFromSerial(char *_inData);

void writeSerial(const uint8_t *buffer, uint8_t length)
{
  Serial.write(buffer, length);
  Serial.flush();
}

void printSerial(String str)
{
  char *outChar = &str[0];
  Serial.print(outChar);
  Serial.println();
  Serial.flush();
}

int getDataFromSerial(char *_inData)
{
  int index = 0;
  while (Serial.available())
  {
    char inChar = Serial.read();
    delayMicroseconds(1300);
    if (index < SERIAL_BUFFER)
    {
      _inData[index] = inChar;
      index++;
    }
  }
  return index;
}

WORK_MODE nextWorkMode(WORK_MODE wm)
{
  switch (wm)
  {
  case MANUAL:
    return AUTO_PV;
  case AUTO_PV:
    return AUTO;
  case AUTO:
    return OFF;
  case OFF:
    return MANUAL;
  }
  return AUTO_PV;
}

bool checkSchedule(DateTime currentTime, ScheduleSlot slot)
{
  char s_time[13];
  char s_time_start[13];
  char s_time_stop[13];

  sprintf(s_time, "%04d%02d%02d%02d%02d", currentTime.year(), currentTime.month(), currentTime.day(), currentTime.hour(), currentTime.minute());
  sprintf(s_time_start, "%04d%02d%02d%02d%02d", currentTime.year(), currentTime.month(), currentTime.day(), slot.slotStart.hour, slot.slotStart.minute);
  sprintf(s_time_stop, "%04d%02d%02d%02d%02d", currentTime.year(), currentTime.month(), currentTime.day(), slot.slotStop.hour, slot.slotStop.minute);
  // Serial.println(s_time_start + " < " + s_time + " < " + s_time_stop);

  if (slot.slotStart.hour <= slot.slotStop.hour)
  {
    return strcmp(s_time, s_time_start) >= 0 && strcmp(s_time, s_time_stop) <= 0;
  }
  else
  {
    return strcmp(s_time, s_time_start) >= 0 || strcmp(s_time, s_time_stop) <= 0;
  }
}

void initialize(RTC_DS3231 rtc, Adafruit_ST7735 tft)
{
  WiFiUDP ntpService;
  NTPClient timeClient(ntpService, "pl.pool.ntp.org");

  // Use this initializer if using a 1.8" TFT screen:

  tft.initR(INITR_BLACKTAB); // Init ST7735S chip, black tab
  tft.setRotation(0);
  tft.setTextWrap(false);

  tft.fillScreen(ST77XX_BLACK);
  tft.invertDisplay(false);
  tft.setTextSize(1);

  PrintD(tft, "WIFI connecting...");
  // Serial.println("WIFI connecting...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);

  WiFi.waitForConnectResult();
  delay(3000);

  if (WiFi.status() == WL_CONNECTED)
  {
    WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), IPAddress(8, 8, 8, 8));
    delay(1000);
    PrintD(tft, "Connected.", 0);
    PrintD(tft, "IP: " + WiFi.localIP().toString(), 1);
    delay(3000);

    timeClient.begin();
    timeClient.setTimeOffset(3600);

    while (!timeClient.update())
    {
      timeClient.forceUpdate();
    };
    unsigned long unix_epoch = timeClient.getEpochTime();
    PrintD(tft, "Initialize RTC...");
    if (unix_epoch > 0)
    {
      DateTime currentTime = DateTime(unix_epoch); // Get current time from RTC
      rtc.adjust(currentTime);
    }
  }
  else
  {
    PrintD(tft, "Error WIFI ");
    delay(5000);
  }
}

void PrintD(Adafruit_ST7735 tft, String str, int line)
{
  if (line == 0 ) {
    tft.fillScreen(ST77XX_BLACK);
  }
  tft.setCursor(0, line == 0 ? 5 : (line * 2 * 9) + 2);
  tft.fillRect(0, (line == 0 ? 5 : (line * 2 * 9) + 2), 160, 11, ST7735_BLACK);
  tft.print(str);
}

String jsonAsString(JsonVariant json)
{
  if (json.isNull())
    return "";
  else 
    return json.as<const char *>();
}

void displayRow(Adafruit_ST7735 tft, int row, int col, String name, String value, String defaultVlue = "")
{
  if (col == -1)
    tft.setCursor(7, row * 10 + 20);
  else
    tft.setCursor(col * 65, row * 10 + 20);

  tft.printf("%s%s", name, value != "" ? value : defaultVlue);
}

void PrintMode(Adafruit_ST7735 tft, WORK_MODE work)
{
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW);
  switch (work)
  {
    case MANUAL:
      tft.setCursor(30, 70);
      tft.print("MANUAL");
      break;
    case AUTO:
      tft.setCursor(40, 70);
      tft.print("AUTO");
      break;
    case AUTO_PV:
      tft.setCursor(50, 70);
      tft.println("PV");
      break;
    case OFF:
      tft.setCursor(30, 70);
      tft.println("CO OFF");
      break;
  }
  tft.setTextColor(ST77XX_WHITE);

  tft.setTextSize(1);
  tft.setCursor(10, 150);
  
  if (WiFi.status() == WL_CONNECTED)
    tft.println("IP: " + WiFi.localIP().toString());
  else
    tft.println("Error WIFI");
}

void PrintAll(Adafruit_ST7735 tft, bool co_on, DateTime rtcTime, JsonDocument hpDoc, PV pv, WORK_MODE work)
{
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(1);

  tft.setCursor(0, 3);
  tft.printf("%04d.%02d.%02d %02d:%02d", rtcTime.year(), rtcTime.month(), rtcTime.day(), rtcTime.hour(), rtcTime.minute());
  tft.setCursor(110, 3);
  switch (work)
  {
    case MANUAL:
      tft.printf("M");
      break;
    case AUTO:
      tft.printf("A");
      break;
    case AUTO_PV:
      tft.printf("PV");
      break;
    case OFF:
      tft.printf("OFF");
  }
  // tft.setCursor(0, 13);
  // tft.println(co_on ? "CO:ON" : "CO:OFF");
  tft.setCursor(105, 33);
  tft.println(co_on ? "ON" : "OFF");

  tft.drawLine(0, 23, 420, 23, ST77XX_BLUE);
  if (!hpDoc.isNull() && !hpDoc["HP"].isNull())
  {
    JsonObject hp = hpDoc["HP"].as<JsonObject>();

    tft.setCursor(0, 13);
    tft.printf("P:%s", pv.total_prod > 0 ? (String(pv.total_power) + "/" + String((float)pv.total_prod_today / 1000, 1) + "k") : "-/-");
    // tft.printf("P:%s", "3000/20.2k");

    tft.setCursor(90, 13);
    tft.printf("T:%s", pv.total_prod > 0 ? String((int)pv.temperature) : "-");

    tft.setCursor(105, 53);
    tft.printf("%s", jsonAsString(hp["CWUS"]) == "1" ? "ON" : "OFF");

    tft.setTextSize(2);

    if (jsonAsString(hp["F"]) == "1")
    {
      tft.setTextColor(ST77XX_YELLOW);
      tft.setCursor(0, 10 + 20);
      tft.printf("%s", "F");
      tft.setTextColor(ST77XX_WHITE);
    }

    // CO + CWU
    if (hp["CO"].isNull())
    {
      displayRow(tft, 1, -1, " CO:", "----");
    }
    else
    {
      if (jsonAsString(hp["HPS"]) == "1")
      {
        tft.setTextColor(ST77XX_RED);
      }
      displayRow(tft, 1, -1, " CO:",
                 jsonAsString(hp["CO"]) == "0" ? "OFF" : jsonAsString(hp["Ttarget"]));
      tft.setTextColor(ST77XX_WHITE);
    }

    if (hp["CWU"].isNull())
    {
      displayRow(tft, 3, -1, "CWU:", "----");
    }
    else
    {
      if (jsonAsString(hp["CWUS"]) == "1")
      {
        tft.setTextColor(ST77XX_RED);
      }
      displayRow(tft, 3, -1, "CWU:",
                 jsonAsString(hp["CWU"]) == "0" ? "OFF" : jsonAsString(hp["Tcwu"]));
      tft.setTextColor(ST77XX_WHITE);
    }

    tft.setTextColor(ST77XX_WHITE);
    tft.drawLine(0, 70, 420, 70, ST77XX_BLUE);
    tft.setTextSize(1);

    int xx = 6;
    displayRow(tft, xx++, 0, "  T. CO: ", jsonAsString(hp["Tmin"]) + " / " + jsonAsString(hp["Tmax"]));
    displayRow(tft, xx++, 0, "  T.CWU: ", jsonAsString(hp["Tcwu_min"]) + " / " + jsonAsString(hp["Tcwu_max"]));

    displayRow(tft, xx, 0, "T.be:", jsonAsString(hp["Tbe"]));
    displayRow(tft, xx++, 1, "T.ae:", jsonAsString(hp["Tae"]));

    displayRow(tft, xx, 0, "T.co:", jsonAsString(hp["Tco"]));
    displayRow(tft, xx++, 1, "T.ho:", jsonAsString(hp["Tho"]));

    displayRow(tft, xx, 0, "T.hp:", jsonAsString(hp["Tsump"]));
    displayRow(tft, xx++, 1, "Watt:", jsonAsString(hp["Watts"]));

    displayRow(tft, xx, 0, "E.ev:", jsonAsString(hp["EEV"]));
    displayRow(tft, xx++, 1, "E.dt:", jsonAsString(hp["EEV_dt"]));

    displayRow(tft, xx, 0, "E.ps:", jsonAsString(hp["EEV_pos"]));
    displayRow(tft, xx++, 1, "HP.s:",
               jsonAsString(hp["HPS"]) == "" ? "" : (jsonAsString(hp["HPS"]) == "1" ? "ON" : "OFF"));

    displayRow(tft, xx, 0, "HC.s:",
               jsonAsString(hp["HCS"]) == "" ? "" : (jsonAsString(hp["HCS"]) == "1" ? "ON" : "OFF"));
    displayRow(tft, xx++, 1, "CC.s:",
               jsonAsString(hp["CCS"]) == "" ? "" : (jsonAsString(hp["CCS"]) == "1" ? "ON" : "OFF"));
  }
}

/*
{"Tbe":"23.6","Tae":"23.3","Tco":"23.7","Tho":"23.4","Ttarget":"24.8","Tsump":"23.9","EEV_dt":"0.0","Tcwu":"25.0","Tmax":"18.5","Tmin":"13.0","Tcwu_max":"26.0","Tcwu_min":"23.0","Watts":"72","EEV":"2.0","EEV_pos":"50","HCS":"0","CCS":"0","HPS":"0","F":"0","CWUS":"0","CWU":"1","CO":"1"}
*/