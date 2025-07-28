#include <RTClib.h>
#include <Wire.h>
#include "WiFi.h"
#include <NTPClient.h>
#include <env.h>
#include <FastCRC.h>

const int SERIAL_BUFFER = 1024;

bool checkSchedule(DateTime currentTime, ScheduleSlot slot);
void initialize(RTC_DS3231 rtc, Adafruit_ST7735 tft);

// print lcd
void PrintD(Adafruit_ST7735 tft, String str, int line = 0);
void PrintAll(Adafruit_ST7735 tft, bool co_on, DateTime rtcTime, JsonDocument hpDoc, PV pv_power);

// serial
void writeSerial(const uint8_t *buffer, uint8_t length);
void printSerial(String str);
int getDataFromSerial(char *_inData);
SERIAL_OPERATION sendRequest(SERIAL_OPERATION so, double value = 0.0f);

String jsonAsString(JsonVariant json);

void writeSerial(const uint8_t *buffer, uint8_t length)
{
  Serial.write(buffer, length);
  Serial.flush();
  // delay(10);
}

void printSerial(String str)
{
  char *outChar = &str[0];
  Serial.print(outChar);
  Serial.println();
  Serial.flush();
  // delay(10);
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
    return CWU;
  case CWU:
    return OFF;
  case OFF:
    return MANUAL;
  }

  return CWU;
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
    timeClient.setTimeOffset(7200);
    // timeClient.setTimeOffset(3600);

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
  tft.clearWriteError();

  switch (work)
  {
    case MANUAL:
      tft.setCursor(20, 70);
      tft.printf("MANUAL");
      break;
    case AUTO:
      tft.setCursor(20, 70);
      tft.printf("AUTO");
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
    tft.printf("IP: %s", WiFi.localIP().toString());
  else
    tft.printf("Error WIFI");
}

void digitalWriteA(Adafruit_ST7735 tft, uint8_t pin, uint8_t val) {
  uint8_t prev = digitalRead(pin); 
  digitalWrite(pin, val);
  if (prev != val) {
    tft.initR(INITR_BLACKTAB); // Init ST7735S chip, black tab
    tft.setRotation(0);
    tft.setTextWrap(false); 
    delay(1000); 
  }
  
}

void PrintAll(Adafruit_ST7735 tft, bool co_on, bool cwu_on, DateTime rtcTime, JsonDocument hpDoc, WORK_MODE work, PV pv_power)
{  
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(1);

  tft.clearWriteError();
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
    case CWU:
      tft.printf("CWU");
      break;
    case OFF:
      tft.printf("OFF");
  }

  tft.drawLine(0, 23, 420, 23, ST77XX_BLUE);
  
  tft.setCursor(0, 13);
  tft.printf("P:%d/%d", pv_power.total_power, pv_power.total_prod_today);

  tft.setCursor(90, 13);
  tft.printf("T:%2.0f", pv_power.temperature);
  
  
  if (!hpDoc.isNull() && !hpDoc["HP"].isNull())
  {
    JsonObject hp = hpDoc["HP"].as<JsonObject>();

    // tft.setCursor(105, 33);
    // tft.println(hp["CO"] ? "ON" : "OFF");
    
    // tft.setCursor(105, 53);
    // tft.printf("%s", hp["CWU"] ? "ON" : "OFF");
    

    tft.setTextSize(2);

    if (hp["F"])
    {
      tft.setTextColor(ST77XX_YELLOW);
      tft.setCursor(0, 10 + 20);
      tft.printf("%s", "F");
      tft.setTextColor(ST77XX_WHITE);
    }

    // CO + CWU
    if (hp["CO"].isNull())
    {
      displayRow(tft, 1, -1, "  T:", "----");
    }
    else
    {
      if (co_on)
      {
        tft.setTextColor(ST77XX_RED);
      }
      displayRow(tft, 1, -1, "  T:", jsonAsString(hp["Ttarget"]));
      tft.setTextColor(ST77XX_WHITE);
    }

    // if (hp["CWU"].isNull())
    // {
    //   displayRow(tft, 3, -1, "CWU:", "----");
    // }
    // else
    // {
    //   if (hp["CWUS"])
    //   {
    //     tft.setTextColor(ST77XX_RED);
    //   }
    //   displayRow(tft, 3, -1, "CWU:", jsonAsString(hp["Tcwu"]));
    //   tft.setTextColor(ST77XX_WHITE);
    // }

    if (cwu_on ) {
      displayRow(tft, 3, -1, "CWU:", "ON");
    } else {
      displayRow(tft, 3, -1, "CWU:", "OFF");
    }

    tft.setTextColor(ST77XX_WHITE);
    tft.drawLine(0, 70, 420, 70, ST77XX_BLUE);
    tft.setTextSize(1);

    int xx = 6;
    displayRow(tft, xx++, 0, "   T. CO:", jsonAsString(hp["Tmin"]) + "/" + jsonAsString(hp["Tmax"]));
    // displayRow(tft, xx++, 0, "   T.CWU:", jsonAsString(hp["Tcwu_min"]) + "/" + jsonAsString(hp["Tcwu_max"]));
    xx++;
    
    displayRow(tft, xx, 0, "T.be:", jsonAsString(hp["Tbe"]));
    displayRow(tft, xx++, 1, "T.ae:", jsonAsString(hp["Tae"]));

    displayRow(tft, xx, 0, "T.co:", jsonAsString(hp["Tco"]));
    displayRow(tft, xx++, 1, "T.ho:", jsonAsString(hp["Tho"]));

    displayRow(tft, xx, 0, "T.hp:", jsonAsString(hp["Tsump"]));
    displayRow(tft, xx++, 1, "Watt:", jsonAsString(hp["Watts"]));

    displayRow(tft, xx, 0, "E.ev:", jsonAsString(hp["EEV"]));
    displayRow(tft, xx++, 1, "E.dt:", jsonAsString(hp["EEV_dt"]));

    displayRow(tft, xx, 0, "E.ps:", jsonAsString(hp["EEV_pos"]));
    displayRow(tft, xx++, 1, "HP.s:", hp["HPS"].isNull() ? "" : hp["HPS"]  ? "ON" : "OFF");
 
    displayRow(tft, xx, 0, "HC.s:",  hp["HCS"].isNull() ? "" : hp["HCS"] ? "ON" : "OFF");
    displayRow(tft, xx++, 1, "CC.s:", hp["CCS"].isNull() ? "" : hp["CCS"] ? "ON" : "OFF");
  }
}


SERIAL_OPERATION sendRequest(SERIAL_OPERATION so, double value)
{
  FastCRC16 CRC16;
  delay(500);
  uint8_t buffer[10];
  unsigned int crcXmodem;

  switch (so)
  {
  case GET_HP_DATA:
    buffer[0] = 0x41;
    buffer[1] = 0x01;
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    break;

  case GET_PV_DATA_1:
    buffer[0] = PV_DEVICE_ID;
    buffer[1] = 0x03;
    buffer[2] = highByte(0x1000);
    buffer[3] = lowByte(0x1000);
    buffer[4] = highByte(0x0280);
    buffer[5] = lowByte(0x0280);
    crcXmodem = CRC16.modbus(buffer, 6);
    buffer[6] = highByte(crcXmodem);
    buffer[7] = lowByte(crcXmodem);
    writeSerial(buffer, 8);
    break;

  case GET_PV_DATA_2:
    buffer[0] = PV_DEVICE_ID;
    buffer[1] = 0x03;
    buffer[2] = highByte(0x1000 + 5 * 40);
    buffer[3] = lowByte(0x1000 + 5 * 40);
    buffer[4] = highByte(0x0320);
    buffer[5] = lowByte(0x0320);
    crcXmodem = CRC16.modbus(buffer, 6);
    buffer[6] = highByte(crcXmodem);
    buffer[7] = lowByte(crcXmodem);
    writeSerial(buffer, 8);
    break;

  case SET_HP_FORCE_ON:
    buffer[0] = 0x41;
    buffer[1] = 0x03;
    buffer[2] = 0x01;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    break;

  case SET_HP_FORCE_OFF:
    buffer[0] = 0x41;
    buffer[1] = 0x03;
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    break;

  case SET_HP_CO_ON:
    buffer[0] = 0x41;
    buffer[1] = 0x0C;
    buffer[2] = 0x01;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    break;

  case SET_HP_CO_OFF:
    buffer[0] = 0x41;
    buffer[1] = 0x0C;
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    break;

  case SET_HP_CWU_ON:
    buffer[0] = 0x41;
    buffer[1] = 0x0D;
    buffer[2] = 0x01;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    break;

  case SET_HP_CWU_OFF:
    buffer[0] = 0x41;
    buffer[1] = 0x0D;
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    break;

  case SET_SUMP_HEATER_ON:
    buffer[0] = 0x41;
    buffer[1] = 0x0B;
    buffer[2] = 0x01;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    break;

  case SET_SUMP_HEATER_OFF:
    buffer[0] = 0x41;
    buffer[1] = 0x0B;
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    break;

  case SET_COLD_POMP_ON:
    buffer[0] = 0x41;
    buffer[1] = 0x0A;
    buffer[2] = 0x01;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    break;

  case SET_COLD_POMP_OFF:
    buffer[0] = 0x41;
    buffer[1] = 0x0A;
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    break;

  case SET_HOT_POMP_ON:
    buffer[0] = 0x41;
    buffer[1] = 0x09;
    buffer[2] = 0x01;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    break;

  case SET_HOT_POMP_OFF:
    buffer[0] = 0x41;
    buffer[1] = 0x09;
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    break;

  case SET_T_SETPOINT_CO:
    buffer[0] = 0x41;
    buffer[1] = 0x04;
    buffer[2] = (uint8_t)value;
    buffer[3] = (uint8_t)roundf((value - (uint8_t)value) * 100.0f);
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    break;

  case SET_T_DELTA_CO:
    buffer[0] = 0x41;
    buffer[1] = 0x05;
    buffer[2] = (uint8_t)value;
    buffer[3] = (uint8_t)roundf((value - (uint8_t)value) * 100.0f);
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    break;
  
  case SET_EEV_MAXPULSES_OPEN:
    buffer[0] = 0x41;
    buffer[1] = 0x0D;
    buffer[2] = (uint8_t)value;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    break;
  
  case SET_WORKING_WATT:
    buffer[0] = 0x41;
    buffer[1] = 0x0E;
    buffer[2] = (uint8_t)(value / 100);
    buffer[3] = (uint8_t)value - (uint8_t)(value/100)*100;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    break;

  case SET_EEV_SETPOINT:
    buffer[0] = 0x41;
    buffer[1] = 0x08;
    buffer[2] = (uint8_t)value;
    buffer[3] = (uint8_t)roundf((value - (uint8_t)value) * 100.0f);
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    break;
  };
  delay(500);

  return so;
}
