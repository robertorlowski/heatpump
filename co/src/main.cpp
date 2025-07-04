#include <Arduino.h>
#include <utils.hpp>
#include <HardwareSerial.h>
#include <env.h>
#include <ArduinoJson.h>
#include <FastCRC.h>
#include <WiFi.h>
#include "web/static_files.h"
#include <WebServer.h>
#include <Preferences.h>


#define PV_DEVICE_ID 0x69
#define PV_count 5
#define HP_FORCE_ON 2000
// #define T_CO_ON 30.0

// const's
const int MILLIS_SCHEDULE = 30000;

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
Preferences prefs;

// temporary variables
unsigned long _millisSchedule = -1;

bool schedule_co = false;
bool schedule_cwu = false;
uint8_t _counter = 0;
bool co_pomp = false;
bool cwu_pomp = false;
bool hp_prev = false;

// global functions
bool schedule(DateTime time, ScheduleSlot *slots, int arraySize);
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

  prefs.begin("hp");

  if (!prefs.isKey("cwu_min")) {
    prefs.putDouble("cwu_min", 40);
  }
  if (!prefs.isKey("cwu_max")) {
    prefs.putDouble("cwu_max", 47);
  }
  if (!prefs.isKey("co_min")) {
    prefs.putDouble("co_min", 35);
  }
  if (!prefs.isKey("co_max")) {
    prefs.putDouble("co_max", 45);
  }

  if (prefs.isKey("workMode")) {
      workMode = (WORK_MODE)prefs.getShort("workMode", WORK_MODE::OFF);
  } else {
     prefs.putShort("workMode", workMode);
  }
}

void loop()
{
  if (digitalRead(SWITCH_POMP_CO))
  {
    delay(1000);
    if (digitalRead(SWITCH_POMP_CO)) {
      workMode = nextWorkMode(workMode);
      prefs.putShort("workMode", workMode);
    }
  
    PrintMode(tft, workMode);
    _millisSchedule = millis() - (MILLIS_SCHEDULE - 2000);
  }

  if ( millis() % 1000 == 0 ) 
  {
    jsonDocument["time"] = rtcTime;
    jsonDocument["co_pomp"] = co_pomp;
    jsonDocument["cwu_pomp"] = cwu_pomp; 
    jsonDocument["pv_power"] = pv.pv_power;
    jsonDocument["schedule_co"] = schedule_co;
    jsonDocument["work_mode"] = workMode;
    jsonDocument["co_min"] = prefs.getDouble("co_min");
    jsonDocument["co_max"] = prefs.getDouble("co_max");
    jsonDocument["cwu_min"] = prefs.getDouble("cwu_min");
    jsonDocument["cwu_max"] = prefs.getDouble("cwu_max");

    
  }
  
  if ((_millisSchedule == -1) || (millis() - _millisSchedule > MILLIS_SCHEDULE))
  {
    _millisSchedule = millis();

    rtcTime = rtc.now(); // Get current time from RTC
    // check co
    schedule_co = schedule(rtcTime, coSlots, (sizeof(coSlots) / sizeof(ScheduleSlot)));
    schedule_cwu = schedule(rtcTime, cwuSlots, (sizeof(cwuSlots) / sizeof(ScheduleSlot)));

    if (prevMode != workMode) {
      sendRequest(SERIAL_OPERATION::SET_HP_FORCE_OFF);
      
      if (workMode == WORK_MODE::OFF) {
        sendRequest(SERIAL_OPERATION::SET_HP_CO_OFF);
        sendRequest(SERIAL_OPERATION ::SET_HOT_POMP_OFF);
      }
      else { 
        sendRequest(SERIAL_OPERATION::SET_HP_CO_ON);
      }
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
      co_pomp = schedule_co;
      cwu_pomp = co_pomp;
      break;
    case WORK_MODE::AUTO_PV:
      co_pomp = schedule_co || pv.pv_power;
      cwu_pomp = co_pomp;
      // cwu_pomp = true;
      break;
    case WORK_MODE::CWU:
      co_pomp = false;
      cwu_pomp = false;
      //cwu_pomp = true;
      break;  
    }
    
    digitalWriteA(tft, RELAY_HP_CWU, cwu_pomp);
    digitalWriteA(tft, RELAY_HP_CO, co_pomp);


    if (workMode != WORK_MODE::OFF) 
    {
      if (!jsonDocument.isNull() && !jsonDocument["HP"].isNull())
      {
        JsonObject hp = jsonDocument["HP"].as<JsonObject>();
        if (schedule_co && workMode != WORK_MODE::CWU 
          && (jsonAsString(hp["Tmin"]).toDouble() != prefs.getDouble("co_min") || jsonAsString(hp["Tmax"]).toDouble() != prefs.getDouble("co_max"))
        ) {
          sendRequest(SERIAL_OPERATION ::SET_T_SETPOINT_CO, prefs.getDouble("co_max")); 
          sendRequest(SERIAL_OPERATION ::SET_T_DELTA_CO, prefs.getDouble("co_max")-prefs.getDouble("co_min")); 
        }
        else if (jsonAsString(hp["Tmin"]).toDouble() != prefs.getDouble("cwu_min") || jsonAsString(hp["Tmax"]).toDouble() != prefs.getDouble("cwu_max")) 
        {
          sendRequest(SERIAL_OPERATION ::SET_T_SETPOINT_CO, prefs.getDouble("cwu_max")); 
          sendRequest(SERIAL_OPERATION ::SET_T_DELTA_CO, prefs.getDouble("cwu_max")-prefs.getDouble("cwu_min")); 
        }

        //włącmy pompę obiegową ciepłej wody by wymieszać wodę w zasobniku
        if (!hp["HCS"] 
          && co_pomp 
          && !hp["Ttarget"].isNull()
          && !hp["Tho"].isNull()
          && (jsonAsString(hp["Ttarget"]).toDouble() - jsonAsString(hp["Tho"]).toDouble() ) > 3 
          && !hp["HCS"].isNull() && !hp["HCS"]
        ) {
          sendRequest(SERIAL_OPERATION ::SET_HOT_POMP_ON);
        } else if (!hp["HCS"].isNull() && hp["HCS"] && co_pomp) {
          sendRequest(SERIAL_OPERATION ::SET_HOT_POMP_OFF);
        }

        if (!hp["HPS"].isNull()) {
          if (hp_prev != hp["HPS"] && !hp_prev ) {
            jsonDocument["t_min"] = hp["Ttarget"];
            jsonDocument["t_max"] = hp["Ttarget"];
            jsonDocument["cop"].clear();
          } 
          hp_prev = hp["HPS"]; 

          JsonObject temp_ = jsonDocument.as<JsonObject>();
          if (hp["HPS"]) {
            if (jsonDocument["t_min"].isNull()) {
              jsonDocument["t_min"] = hp["Ttarget"]; 
            } else if (jsonAsString(hp["Ttarget"]).toDouble() < jsonAsString(temp_["t_min"]).toDouble()) {
              jsonDocument["t_min"] = hp["Ttarget"]; 
            } 
          } else {
            if (!jsonDocument["t_min"].isNull() 
              && !jsonDocument["t_max"].isNull() 
              && !hp["last_power"].isNull() 
              && !hp["last_heatpump_on"].isNull() )
            {
              double t_min = jsonAsString(jsonDocument["t_min"]).toDouble();
              double t_max = jsonAsString(jsonDocument["t_max"]).toDouble();
              double last_power = jsonAsString(hp["last_power"]).toDouble();
              double last_heatpump_on = jsonAsString(hp["last_heatpump_on"]).toDouble();

              double wymiennik = (4200 * 300 * (t_max - t_min)) / (last_heatpump_on / 3600);   

              jsonDocument["cop"] = round((wymiennik / last_power)*100) / 100;
            }
          }


          if (jsonDocument["t_max"].isNull()) {
            jsonDocument["t_max"] = hp["Ttarget"]; 
          } else if (jsonAsString(hp["Ttarget"]).toDouble() > jsonAsString(temp_["t_max"]).toDouble()) {
            jsonDocument["t_max"] = hp["Ttarget"]; 
          } 
          
        }
      }
    

      if (schedule_cwu) {
        sendRequest(SERIAL_OPERATION::SET_HP_FORCE_ON);
      } else {
        sendRequest(SERIAL_OPERATION::SET_HP_FORCE_OFF);
      } 
    }

    // print ALL
    PrintAll(tft, co_pomp, cwu_pomp, rtcTime, jsonDocument, workMode, pv);

    if (workMode == WORK_MODE::MANUAL && checkSchedule(rtcTime, updateManualMode))
    {
      workMode = (WORK_MODE)prefs.getShort("workMode", WORK_MODE::OFF);
      PrintMode(tft, workMode);
      _millisSchedule = millis() - (MILLIS_SCHEDULE - 2000);
    }

    // get data from PV + HP
    if (_counter % 10 == 0)
    {
        pv.total_power = 0;
        pv.total_prod = 0;
        pv.total_prod_today = 0;
        pv.temperature = 0;
        jsonDocument["PV"] = emptyDoc;
        sendRequest(SERIAL_OPERATION::GET_PV_DATA_1);
    }
    else
    {
      jsonDocument["HP"] = emptyDoc;
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
      if (serialOpertion == SERIAL_OPERATION::GET_PV_DATA_1) {
        collectDataFromPV(inData);
        sendRequest(SERIAL_OPERATION::GET_PV_DATA_2);
      } 
      else if (serialOpertion == SERIAL_OPERATION::GET_PV_DATA_2)
      {        
        collectDataFromPV(inData);
        if (pv.pv_power && (pv.total_power >= HP_FORCE_ON))
        {
          sendRequest( (pv.pv_power  && (workMode == AUTO_PV) ) ? 
            SERIAL_OPERATION::SET_HP_FORCE_ON : 
            SERIAL_OPERATION::SET_HP_FORCE_OFF);
        } 
        else 
        {
          sendRequest(SERIAL_OPERATION::SET_HP_FORCE_OFF);
        }
        pv.pv_power = pv.total_power >= HP_FORCE_ON;
        jsonDocument["pv_power"] = pv.pv_power;
        jsonDocument["PV"] = pv;
        
        // printSerial(jsonDocument["PV"]);
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
    // printSerial("NR " + String(i) + " total_power " + String(_getPVData(i, inData, 19, 2)/10));
    pv.total_power += _getPVData(i, inData, 19, 2)/10;

    // printSerial("NR " + String(i) + " total_prod_today " +  String(_getPVData(i, inData, 21, 2)));
    pv.total_prod_today += _getPVData(i, inData, 21, 2);

    // printSerial("NR " + String(i) + " total_prod " +  String(_getPVData(i, inData, 23, 4)));
    pv.total_prod += _getPVData(i, inData, 23, 4);

    // printSerial("NR " + String(i) + " temperature 0 " +  String(_getPVData(i, inData, 27, 2) ));
    // printSerial("NR " + String(i) + " temperature 1 " +  String(_getPVData(i, inData, 27, 1) ));
    // printSerial("NR " + String(i) + " temperature 2 " +  String(_getPVData(i, inData, 28, 1) ));

    pv.temperature = _getPVData(i, inData, 27, 2) / 10;   
    // printSerial("NR " + String(i) + " temperature " +  String(((_getPVData(i, inData, 28, 1) + _getPVData(i, inData, 27, 1)) / 10)));
    // pv.temperature = ((_getPVData(i, inData, 28, 1) + _getPVData(i, inData, 27, 1)) / 10);
  }
}

bool schedule(DateTime time, ScheduleSlot *slots, int arraySize)
{
  bool _ret = false;
  for (int i = 0; (i < arraySize && !_ret); i++)
  {
    _ret = _ret || checkSchedule(time, slots[i]);
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
  u8_t size = sizeof(coSlots) / sizeof(ScheduleSlot);
  JsonDocument jsonscheduleSlots;
  JsonArray arrayJsonscheduleSlots = jsonscheduleSlots.to<JsonArray>();
  for (int i = 0; i < size; i++)
  {
    arrayJsonscheduleSlots.add(coSlots[i]);
  }
  
  size = sizeof(cwuSlots) / sizeof(ScheduleSlot);
  JsonDocument jsonCwuSlots;
  JsonArray arrayJsonCwuSlots = jsonCwuSlots.to<JsonArray>();
  for (int i = 0; i < size; i++)
  {
    arrayJsonCwuSlots.add(cwuSlots[i]);
  }
  
  
  JsonDocument jsonSettings;
  jsonSettings["settings"] = jsonscheduleSlots;
  jsonSettings["cwu_settings"] = arrayJsonCwuSlots;
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
  // printSerial("Operation:" + String(so));
  delay(1000);
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
    return;

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
    return;

  case SET_HP_FORCE_ON:
    buffer[0] = 0x41;
    buffer[1] = 0x03;
    buffer[2] = 0x01;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    return;

  case SET_HP_FORCE_OFF:
    buffer[0] = 0x41;
    buffer[1] = 0x03;
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    return;

  case SET_HP_CO_ON:
    buffer[0] = 0x41;
    buffer[1] = 0x0C;
    buffer[2] = 0x01;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    return;

  case SET_HP_CO_OFF:
    buffer[0] = 0x41;
    buffer[1] = 0x0C;
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    return;

  case SET_HP_CWU_ON:
    buffer[0] = 0x41;
    buffer[1] = 0x0D;
    buffer[2] = 0x01;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    return;

  case SET_HP_CWU_OFF:
    buffer[0] = 0x41;
    buffer[1] = 0x0D;
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    return;

  case SET_SUMP_HEATER_ON:
    buffer[0] = 0x41;
    buffer[1] = 0x0B;
    buffer[2] = 0x01;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    return;

  case SET_SUMP_HEATER_OFF:
    buffer[0] = 0x41;
    buffer[1] = 0x0B;
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    return;

  case SET_COLD_POMP_ON:
    buffer[0] = 0x41;
    buffer[1] = 0x0A;
    buffer[2] = 0x01;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    return;

  case SET_COLD_POMP_OFF:
    buffer[0] = 0x41;
    buffer[1] = 0x0A;
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    return;

  case SET_HOT_POMP_ON:
    buffer[0] = 0x41;
    buffer[1] = 0x09;
    buffer[2] = 0x01;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    return;

  case SET_HOT_POMP_OFF:
    buffer[0] = 0x41;
    buffer[1] = 0x09;
    buffer[2] = 0x00;
    buffer[3] = 0x00;
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    return;

  case SET_T_SETPOINT_CO:
    buffer[0] = 0x41;
    buffer[1] = 0x04;
    buffer[2] = (uint8_t)value;
    buffer[3] = (uint8_t)roundf((value - (uint8_t)value) * 100.0f);
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    return;

  case SET_T_DELTA_CO:
    buffer[0] = 0x41;
    buffer[1] = 0x05;
    buffer[2] = (uint8_t)value;
    buffer[3] = (uint8_t)roundf((value - (uint8_t)value) * 100.0f);
    buffer[4] = 0xFF;
    writeSerial(buffer, 5);
    return;
  }
  
}

void serverRoute(void) {
  server.on("/api/operation", []
    {
      if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method Not Allowed");
        return;
      }

      JsonDocument ddd;
      DeserializationError error = deserializeJson(ddd, server.arg("plain"));
      if (error) {
        server.send(405, "text/plain", "Bad JSON");
        return;
      }
      // printSerial(server.arg("plain"));
      // delay(300);

      JsonObject doc = ddd.as<JsonObject>();
      //work_mode
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

      //co_min
      if (!doc["co_min"].isNull() && jsonAsString(doc["co_min"]) != "" && jsonAsString(doc["co_min"]).toDouble() > 0) {
        if (jsonAsString(doc["co_min"]).toDouble() < 0) {
          return;
        }
        prefs.putDouble("co_min", jsonAsString(doc["co_min"]).toDouble());      
      }
      //co_max
      if (!doc["co_max"].isNull() && jsonAsString(doc["co_max"]) != "" && jsonAsString(doc["co_max"]).toDouble() > 0) {
        if (jsonAsString(doc["co_max"]).toDouble() < 0  || jsonAsString(doc["co_max"]).toDouble() > 50) {
          return;
        }
        prefs.putDouble("co_max", jsonAsString(doc["co_max"]).toDouble());
      }
      //cwu_min
      if (!doc["cwu_min"].isNull() && jsonAsString(doc["cwu_min"]) != "" && jsonAsString(doc["cwu_min"]).toDouble() > 0) {
        if (jsonAsString(doc["cwu_min"]).toDouble() < 0) {
          return;
        }
        prefs.putDouble("cwu_min", jsonAsString(doc["cwu_min"]).toDouble());
      }
      //cwu_max
      if (!doc["cwu_max"].isNull() && jsonAsString(doc["cwu_max"]) != "" && jsonAsString(doc["cwu_max"]).toDouble() > 0) {
        if (jsonAsString(doc["cwu_max"]).toDouble() < 0  || jsonAsString(doc["cwu_max"]).toDouble() > 50) {
          return;
        }
        prefs.putDouble("cwu_max", jsonAsString(doc["cwu_max"]).toDouble());
      }
      
      //sump_heater
      if (!doc["sump_heater"].isNull() && jsonAsString(doc["sump_heater"]) != "" ) 
      {
        (jsonAsString(doc["sump_heater"]) == "1") ?
          sendRequest(SERIAL_OPERATION ::SET_SUMP_HEATER_ON) :
          sendRequest(SERIAL_OPERATION ::SET_SUMP_HEATER_OFF);
        delay(1000);
      }

      //cold_pomp
      if (!doc["cold_pomp"].isNull() && jsonAsString(doc["cold_pomp"]) != "" ) 
      {
        (jsonAsString(doc["cold_pomp"]) == "1") ? 
          sendRequest(SERIAL_OPERATION ::SET_COLD_POMP_ON) :
          sendRequest(SERIAL_OPERATION ::SET_COLD_POMP_OFF);
        delay(1000);
      }

      //hot_pomp
      if (!doc["hot_pomp"].isNull() && jsonAsString(doc["hot_pomp"]) != "" ) 
      {
        (jsonAsString(doc["hot_pomp"]) == "1") ?
          sendRequest(SERIAL_OPERATION ::SET_HOT_POMP_ON) : 
          sendRequest(SERIAL_OPERATION ::SET_HOT_POMP_OFF);
        delay(1000);
      }

      //force
      if (!doc["force"].isNull() && jsonAsString(doc["force"]) != "" ) 
      {
        (jsonAsString(doc["force"]) == "1") ?
          sendRequest(SERIAL_OPERATION ::SET_HP_FORCE_ON) : 
          sendRequest(SERIAL_OPERATION ::SET_HP_FORCE_OFF);
        delay(1000);
      }

      String data = "";
      serializeJsonPretty(doc, data);
      server.send(200, "application/json", data);
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
    }
  );

  server.on("/", []
    {
      server.sendHeader("Content-Encoding", "gzip");
      server.send_P(200, "text/html", (const char *)static_files::f_index_html_contents, static_files::f_index_html_size); 
    }
  );
  server.on("/settings", []
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

// void forceRefresh(void) {
//   _counter = 0;
//   _millisSchedule -= MILLIS_SCHEDULE;
// }

/*
{"Tbe":"23.6","Tae":"23.3","Tco":"23.7","Tho":"23.4","Ttarget":"24.8","Tsump":"23.9","EEV_dt":"0.0","Tcwu":"25.0","Tmax":"18.5","Tmin":"13.0","Tcwu_max":"26.0","Tcwu_min":"23.0","Watts":"72","EEV":"2.0","EEV_pos":"50","HCS":0,"CCS":0,"HPS":0,"F":0,"CWUS":0,"CWU":1,"CO":1}
*/