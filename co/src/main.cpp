#include <Arduino.h>
#include <utils.hpp>
#include <HardwareSerial.h>
#include <env.h>
#include <ArduinoJson.h>
#include <FastCRC.h>
#include <WiFi.h>
#include "web/static_files.h"
// https://github.com/isaackoz/vite-plugin-preact-esp32
#include <WebServer.h>


#define PV_DEVICE_ID 0x69
#define PV_count 5
#define HP_FORCE_ON 2000
// #define T_CO_ON 30.0

// const's
const int MILLIS_SCHEDULE = 10000;

const char devID = 0x10;
const size_t JSON_BUFFER_SIZE = 1024;

// global variables
FastCRC16 CRC16;
RTC_DS3231 rtc;
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_CLK, TFT_RST);
JsonDocument jsonDocument;
DateTime rtcTime;
PV pv;
SERIAL_OPERATION serialOpertion;
WORK_MODE workMode = OFF;
WORK_MODE prevMode = MANUAL;
JsonDocument emptyDoc;
WebServer server(80);

// temporary variables
unsigned long _millisSchedule = -1;

bool schedule_on = false;
bool pv_power = false;
uint8_t _counter = 0;
bool co_pomp = false;
bool cwu_pomp = false;

// global functions
bool schedule(DateTime rtcTime);
JsonDocument settings(void);
void sendDataToSerial(char operation);
long _getPVData(uint8_t pv_nr, char *inData, uint8_t b_start, uint8_t b_count);
void collectDataFromPV(char inData[1024]);
void sendRequest(SERIAL_OPERATION so, double value = 0.0f);
void collectDataFromSerial();
void serverRoute(void);
void forceRefresh(void);

// main
void setup()
{
  Serial.setRxBufferSize(SERIAL_BUFFER);
  Serial.begin(9600); // Initialize serial communication with a baud rate of 9600

  Wire.begin();
  rtc.begin();

  pinMode(RELAY_HP_CWU, OUTPUT);
  pinMode(RELAY_HP_CO, OUTPUT);
  pinMode(PWR, OUTPUT);

  digitalWrite(PWR, HIGH);
  pinMode(SWITCH_POMP_CO, INPUT);

  initialize(rtc, tft);

  deserializeJson(emptyDoc, "{}");
  jsonDocument["HP"] = emptyDoc;
  jsonDocument["PV"] = emptyDoc;

  serverRoute();
  server.begin();
}

void loop()
{
  if (digitalRead(SWITCH_POMP_CO))
  {
    delay(500);
    if (digitalRead(SWITCH_POMP_CO))
      workMode = nextWorkMode(workMode);
  
    PrintMode(tft, workMode);
    _millisSchedule = millis() - (MILLIS_SCHEDULE - 2000);
  }

  if ((_millisSchedule == -1) || (millis() - _millisSchedule > MILLIS_SCHEDULE))
  {
    _millisSchedule = millis();

    rtcTime = rtc.now(); // Get current time from RTC
    // check co
    schedule_on = schedule(rtcTime);

    if (prevMode != workMode) {
      if (workMode == WORK_MODE::OFF) 
        sendRequest(SERIAL_OPERATION::SET_HP_CO_OFF);
      else 
        sendRequest(SERIAL_OPERATION::SET_HP_CO_ON);
    }
    prevMode = workMode;

    switch (workMode)
    {
    case WORK_MODE::OFF:
      co_pomp = false;
      cwu_pomp = false;
      break;
    case WORK_MODE::MANUAL:
      co_pomp = true;
      cwu_pomp = true;
      break;
    case WORK_MODE::AUTO:
      co_pomp = schedule_on;
      cwu_pomp = true;
      break;
    case WORK_MODE::AUTO_PV:
      cwu_pomp = true;
      co_pomp = schedule_on || pv_power;
      break;
    case WORK_MODE::CWU:
      co_pomp = false;
      cwu_pomp = true;
      break;  
    }

    digitalWriteA(tft, RELAY_HP_CWU, cwu_pomp);
    digitalWriteA(tft, RELAY_HP_CO, co_pomp);

    jsonDocument["time"] = rtcTime;
    jsonDocument["co_pomp"] = co_pomp;
    jsonDocument["cwu_pomp"] = cwu_pomp; 
    jsonDocument["pv_power"] = pv_power;
    jsonDocument["schedule_on"] = schedule_on;
    jsonDocument["work_mode"] = workMode;

    // print ALL
    PrintAll(tft, co_pomp, cwu_pomp, rtcTime, jsonDocument, workMode, pv_power);

    if (workMode == WORK_MODE::MANUAL && checkSchedule(rtcTime, setAutoMode))
    {
      workMode = WORK_MODE::AUTO;
      PrintMode(tft, workMode);
      _millisSchedule = millis() - (MILLIS_SCHEDULE - 2000);
    }

    // get data from PV + HP
    if (_counter % 30 == 0)
    {
      pv.total_power = 0;
      pv.total_prod = 0;
      pv.total_prod_today = 0;
      pv.temperature = 0;

      if (checkSchedule(rtcTime, nightHour))
      {
        jsonDocument["PV"] = emptyDoc;
        pv_power = 0;
        sendRequest(SERIAL_OPERATION::SET_HP_FORCE_OFF);
      }
      else
      {
        sendRequest(SERIAL_OPERATION::SET_HP_FORCE_OFF);
        sendRequest(SERIAL_OPERATION::GET_PV_DATA_1);
      }
    }
    else
    {
      sendRequest(SERIAL_OPERATION::GET_HP_DATA);
    }

    _counter++;
  }

  // collect data from serial
  if (Serial.available())
  {
    collectDataFromSerial();
  }

  server.handleClient();
}

void collectDataFromSerial()
{
  char inData[SERIAL_BUFFER];
  int len = getDataFromSerial(inData);

  if ((inData[0] == devID) && (inData[3] == 0xFF))
  {
    sendDataToSerial(inData[1]);
  }
  else if (inData[0] == PV_DEVICE_ID &&
           (serialOpertion == SERIAL_OPERATION::GET_PV_DATA_1 ||
            serialOpertion == SERIAL_OPERATION::GET_PV_DATA_2))
  {
    if (inData[1] == 0x03)
    {
      collectDataFromPV(inData);
      if (serialOpertion == SERIAL_OPERATION::GET_PV_DATA_2)
      {        
        pv_power = pv.total_power >= HP_FORCE_ON;
        jsonDocument["pv_power"] = pv_power;
        sendRequest( (pv_power  && (workMode == AUTO_PV || workMode == CWU) ) ? SERIAL_OPERATION::SET_HP_FORCE_ON : SERIAL_OPERATION::SET_HP_FORCE_OFF);
        jsonDocument["PV"] = pv;
      }
      else
      {
        sendRequest(SERIAL_OPERATION::GET_PV_DATA_2);
      }
    }
  }
  else if (serialOpertion == SERIAL_OPERATION::GET_HP_DATA)
  {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, inData);
    if (!error) {
      jsonDocument["HP"] = doc;
    }
  }
}

void collectDataFromPV(char inData[1024])
{
  for (int i = 0; i < PV_count; i++)
  {
    pv.total_power += _getPVData(i, inData, 19, 2) / 10;
    pv.total_prod_today += _getPVData(i, inData, 21, 2);
    pv.total_prod += _getPVData(i, inData, 23, 4);
    pv.temperature += (_getPVData(i, inData, 28, 1) - _getPVData(i, inData, 27, 1)) / 10;
  }
  pv.temperature = pv.temperature / PV_count;

}

bool schedule(DateTime rtcTime)
{
  bool _ret = false;
  int arraySize = sizeof(scheduleSlot) / sizeof(ScheduleSlot);
  for (int i = 0; (i < arraySize && !_ret); i++)
  {
    _ret = _ret || checkSchedule(rtcTime, scheduleSlot[i]);
  }
  return _ret;
}

void sendDataToSerial(char operation)
{
  String data = "";
  switch (operation)
  {
  case 0x01:
    serializeJsonPretty(jsonDocument, data);
    break;
  case 0x02:
    serializeJsonPretty(settings(), data);
    break;
  case 0x03:
    // NOP
    break;
  default:
    JsonDocument doc;
    doc["error"] = 2;
    serializeJsonPretty(doc, data);
    break;
  }
  printSerial(data);
}

JsonDocument settings(void)
{
  u8_t size = sizeof(scheduleSlot) / sizeof(ScheduleSlot);
  JsonDocument jsonscheduleSlots;
  JsonArray arrayJsonscheduleSlots = jsonscheduleSlots.to<JsonArray>();
  for (int i = 0; i < size; i++)
  {
    arrayJsonscheduleSlots.add(scheduleSlot[i]);
  }
  JsonDocument jsonSettings;
  jsonSettings["settings"] = jsonscheduleSlots;
  jsonSettings["night_hour"] = nightHour;
  return jsonSettings;
}

long _getPVData(uint8_t pv_nr, char *inData, uint8_t b_start, uint8_t b_count)
{
  long ret = 0;
  for (int i = 0; i < b_count; i++)
    ret += (int)inData[b_start + i + pv_nr * 40] << ((b_count - i - 1) * 8);
  return ret;
}

void sendRequest(SERIAL_OPERATION so, double value)
{
  uint8_t buffer[10];
  unsigned int crcXmodem;
  serialOpertion = so;

  switch (so)
  {
  case GET_HP_DATA:
    buffer[0] = 0x41;
    buffer[1] = 0x01;
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    return;

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
  }
  delay(200);
}

void serverRoute(void) {
  server.on("/api/operation", []
    {
      if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method Not Allowed");
        return;
      }

      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, server.arg("plain"));
      if (error) {
        server.send(405, "text/plain", "Bad JSON");
        return;
      }

      // if (!doc["CO"].isNull() && doc["CO"].is<const bool>()) {
      //   sendRequest( doc["CO"].as<const bool>() ? SERIAL_OPERATION::SET_HP_CO_ON : SERIAL_OPERATION::SET_HP_CO_OFF);
      // }
      // if (!doc["CWU"].isNull() && doc["CWU"].is<const bool>()) {
      //   sendRequest( doc["CWU"].as<const bool>() ? SERIAL_OPERATION::SET_HP_CWU_ON : SERIAL_OPERATION::SET_HP_CWU_OFF);
      // }
      // if (!doc["force"].isNull() && doc["force"].is<const bool>()) {
      //   sendRequest(doc["force"].as<const bool>() ? SERIAL_OPERATION::SET_HP_FORCE_ON : SERIAL_OPERATION::SET_HP_FORCE_OFF);
      // }

      if (!doc["work_mode"].isNull()) {
        jsonAsString(doc["work_mode"]) == "M" ?
          workMode = WORK_MODE::MANUAL :
          jsonAsString(doc["work_mode"]) == "A" ?
            workMode = WORK_MODE::AUTO :
            jsonAsString(doc["work_mode"]) == "PV" ?
              workMode = WORK_MODE::AUTO_PV :
              jsonAsString(doc["work_mode"]) == "CWU" ?
                workMode = WORK_MODE::CWU :
                workMode = WORK_MODE::OFF;  
      }

      if (!doc["temperature_co_max"].isNull()) {
        sendRequest(SERIAL_OPERATION ::SET_T_SETPOINT_CO, doc["temperature_co_max"].as<double>());
      }

      if (!doc["temperature_co_min"].isNull()) {
        sendRequest(SERIAL_OPERATION ::SET_T_DELTA_CO, 
          doc["temperature_co_max"].as<double>() - doc["temperature_co_min"].as<double>()
        );
      }
      
      if (!doc["sump_heater"].isNull()) {
        if (doc["sump_heater"].as<boolean>()) 
          sendRequest(SERIAL_OPERATION ::SET_SUMP_HEATER_ON);
        else
          sendRequest(SERIAL_OPERATION ::SET_SUMP_HEATER_OFF);
      }

      if (!doc["cold_pomp"].isNull()) {
        if (doc["cold_pomp"].as<boolean>()) 
          sendRequest(SERIAL_OPERATION ::SET_COLD_POMP_ON);
        else
          sendRequest(SERIAL_OPERATION ::SET_COLD_POMP_OFF);
      }

      if (!doc["hot_pomp"].isNull()) {
        if (doc["hot_pomp"].as<boolean>()) 
          sendRequest(SERIAL_OPERATION ::SET_HOT_POMP_ON);
        else
          sendRequest(SERIAL_OPERATION ::SET_HOT_POMP_OFF);
      }

      //force refresh
      forceRefresh();
      server.send(200);
    }
  );

  server.on("/api/settings",  HTTP_GET, []
    {
      String data = "";
      serializeJsonPretty(settings(), data);
      server.send(200, "application/json", data);
    }
  );

  server.on("/api/hp", []
    {
      String data = "";
      serializeJsonPretty(jsonDocument, data);
      server.send(200, "application/json", data);
      forceRefresh();
    }
  );

  server.on("/", []
    {
      server.sendHeader("Content-Encoding", "gzip");
      server.send_P(200, "text/html", (const char *)static_files::f_index_html_contents, static_files::f_index_html_size); 
    }
  );

  // Create a route handler for each of the build artifacts
  for (int i = 0; i < static_files::num_of_files; i++)
  {
    server.on(static_files::files[i].path, [i]
        {
          server.sendHeader("Content-Encoding", "gzip");
          server.send_P(200, static_files::files[i].type, (const char *)static_files::files[i].contents, static_files::files[i].size); 
        }
    );
  }
}

void forceRefresh(void) {
  _counter = 0;
  _millisSchedule -= MILLIS_SCHEDULE;
}

/*
{"Tbe":"23.6","Tae":"23.3","Tco":"23.7","Tho":"23.4","Ttarget":"24.8","Tsump":"23.9","EEV_dt":"0.0","Tcwu":"25.0","Tmax":"18.5","Tmin":"13.0","Tcwu_max":"26.0","Tcwu_min":"23.0","Watts":"72","EEV":"2.0","EEV_pos":"50","HCS":0,"CCS":0,"HPS":0,"F":0,"CWUS":0,"CWU":1,"CO":1}
*/