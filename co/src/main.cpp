#include <Arduino.h>
#include <utils.hpp>
#include <HardwareSerial.h>
#include <env.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "web/static_files.h"
#include <WebServer.h>
#include <HTTPClient.h>
#include <Preferences.h>


#define PV_count 5
#define HP_FORCE_ON 2000
// #define T_CO_ON 30.0

// const's
constexpr int MILLIS_SCHEDULE = 30000;


const size_t JSON_BUFFER_SIZE = 1024;

// global variables
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
HTTPClient http;

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
void collectDataFromSerial();
void serverRoute(void);
void forceRefresh(void);
void putHpDataToCloud(void);
void operationExecute(JsonDocument doc);
void putHpDataToCloud(void);
String putDataToCloud(String path, JsonDocument data);
void operationExecute(JsonDocument ddd);

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

  //Zapis danych ustawień do chmury:
  putDataToCloud("/settings/set", settings());
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


    JsonObject hp = jsonDocument["HP"].as<JsonObject>();
    if ( !hp.isNull() ) {
      if (hp["CO"] == 0) {
        if (workMode != WORK_MODE::OFF) {
          serialOpertion = sendRequest(SERIAL_OPERATION::SET_HP_CO_ON);
        }

      }

      if (hp["CO"] == 1) {
        if (workMode == WORK_MODE::OFF) {
          serialOpertion = sendRequest(SERIAL_OPERATION::SET_HP_CO_OFF);
          serialOpertion = sendRequest(SERIAL_OPERATION::SET_HP_FORCE_OFF);
        
        } else if (workMode == WORK_MODE::CWU) {
            if (schedule_cwu) {
              serialOpertion = sendRequest(SERIAL_OPERATION::SET_HP_FORCE_ON);
            } else {
              serialOpertion = sendRequest(SERIAL_OPERATION::SET_HP_FORCE_OFF);
            } 
        }
      }

      if (schedule_co && workMode != WORK_MODE::CWU 
        && (jsonAsString(hp["Tmin"]).toDouble() != prefs.getDouble("co_min") || jsonAsString(hp["Tmax"]).toDouble() != prefs.getDouble("co_max"))
      ) {
        serialOpertion = sendRequest(SERIAL_OPERATION ::SET_T_SETPOINT_CO, prefs.getDouble("co_max")); 
        serialOpertion = sendRequest(SERIAL_OPERATION ::SET_T_DELTA_CO, prefs.getDouble("co_max")-prefs.getDouble("co_min")); 
      }
      else if (jsonAsString(hp["Tmin"]).toDouble() != prefs.getDouble("cwu_min") || jsonAsString(hp["Tmax"]).toDouble() != prefs.getDouble("cwu_max")) 
      {
        serialOpertion = sendRequest(SERIAL_OPERATION ::SET_T_SETPOINT_CO, prefs.getDouble("cwu_max")); 
        serialOpertion = sendRequest(SERIAL_OPERATION ::SET_T_DELTA_CO, prefs.getDouble("cwu_max")-prefs.getDouble("cwu_min")); 
      }

      //Włączamy pompę obiegową ciepłej wody, aby wymieszać wodę w zasobniku, gdy jest włączone CO
      if (!hp["HCS"] 
        && co_pomp 
        && !hp["Ttarget"].isNull()
        && !hp["Tho"].isNull()
        && (jsonAsString(hp["Ttarget"]).toDouble() - jsonAsString(hp["Tho"]).toDouble() ) > 3 
        && !hp["HCS"].isNull() && !hp["HCS"]
      ) {
        serialOpertion = sendRequest(SERIAL_OPERATION ::SET_HOT_POMP_ON);
      } else if (!hp["HCS"].isNull() && hp["HCS"] && co_pomp) {
        serialOpertion = sendRequest(SERIAL_OPERATION ::SET_HOT_POMP_OFF);
      }

      if (!hp["HPS"].isNull()) {
        if (hp_prev != hp["HPS"] && !hp_prev ) {
          jsonDocument["t_min"] = hp["Ttarget"];
          jsonDocument["t_max"] = hp["Ttarget"];
          jsonDocument["cop"].clear();
        } 
        hp_prev = hp["HPS"]; 
      }

      JsonObject temp_ = jsonDocument.as<JsonObject>();
      if (!hp["Ttarget"].isNull()) {
        if (jsonDocument["t_min"].isNull()) {
          jsonDocument["t_min"] = hp["Ttarget"]; 
        } else if (jsonAsString(hp["Ttarget"]).toDouble() < jsonAsString(temp_["t_min"]).toDouble()) {
          jsonDocument["t_min"] = hp["Ttarget"]; 
        } 
          
        if (jsonDocument["t_max"].isNull()) {
          jsonDocument["t_max"] = hp["Ttarget"]; 
        } else if (jsonAsString(hp["Ttarget"]).toDouble() > jsonAsString(temp_["t_max"]).toDouble()) {
          jsonDocument["t_max"] = hp["Ttarget"]; 
        } 
      }

      if (!jsonDocument["t_min"].isNull() 
        && !jsonDocument["t_max"].isNull() 
        && !hp["lt_pow"].isNull() 
        && !hp["lt_hp_on"].isNull() )
      {
        double t_min = jsonAsString(jsonDocument["t_min"]).toDouble();
        double t_max = jsonAsString(jsonDocument["t_max"]).toDouble();
        double last_power = jsonAsString(hp["lt_pow"]).toDouble();
        double last_heatpump_on = jsonAsString(hp["lt_hp_on"]).toDouble();

        double wymiennik =  (double)(1.166) * 300 * (t_max - t_min);   
        jsonDocument["cop"] = round((wymiennik / last_power)*100) / 100;
      }
    }

    // print ALL
    PrintAll(tft, co_pomp, cwu_pomp, rtcTime, jsonDocument, workMode, pv);

    if (!jsonDocument.isNull() && !jsonDocument["HP"].isNull()) {
      putHpDataToCloud();
    }

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
        serialOpertion = sendRequest(SERIAL_OPERATION::GET_PV_DATA_1);
    }
    else
    {
      jsonDocument["HP"] = emptyDoc;
      serialOpertion = sendRequest(SERIAL_OPERATION::GET_HP_DATA);
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
        serialOpertion = sendRequest(SERIAL_OPERATION::GET_PV_DATA_2);
      } 
      else if (serialOpertion == SERIAL_OPERATION::GET_PV_DATA_2)
      {        
        collectDataFromPV(inData);
        if (pv.pv_power && (pv.total_power >= HP_FORCE_ON))
        {
         serialOpertion =  sendRequest( (pv.pv_power  && (workMode == AUTO_PV) ) ? 
            SERIAL_OPERATION::SET_HP_FORCE_ON : 
            SERIAL_OPERATION::SET_HP_FORCE_OFF);
        } 
        else 
        {
         serialOpertion = sendRequest(SERIAL_OPERATION::SET_HP_FORCE_OFF);
        }
        pv.pv_power = pv.total_power >= HP_FORCE_ON;
        jsonDocument["pv_power"] = pv.pv_power;
        jsonDocument["PV"] = pv;
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
      operationExecute(ddd);
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

void operationExecute(JsonDocument ddd) {
  
  JsonObject doc  = ddd.as<JsonObject>();

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
      serialOpertion = sendRequest(SERIAL_OPERATION::SET_SUMP_HEATER_ON) :
      serialOpertion = sendRequest(SERIAL_OPERATION::SET_SUMP_HEATER_OFF);
  }

  //cold_pomp
  if (!doc["cold_pomp"].isNull() && jsonAsString(doc["cold_pomp"]) != "" ) 
  {
    (jsonAsString(doc["cold_pomp"]) == "1") ? 
      serialOpertion = sendRequest(SERIAL_OPERATION::SET_COLD_POMP_ON) :
      serialOpertion = sendRequest(SERIAL_OPERATION::SET_COLD_POMP_OFF);
  }

  //hot_pomp
  if (!doc["hot_pomp"].isNull() && jsonAsString(doc["hot_pomp"]) != "" ) 
  {
    (jsonAsString(doc["hot_pomp"]) == "1") ?
      serialOpertion = sendRequest(SERIAL_OPERATION::SET_HOT_POMP_ON) : 
      serialOpertion = sendRequest(SERIAL_OPERATION::SET_HOT_POMP_OFF);
  }

  //force
  if (!doc["force"].isNull() && jsonAsString(doc["force"]) != "" ) 
  {
    (jsonAsString(doc["force"]) == "1") ?
      serialOpertion = sendRequest(SERIAL_OPERATION::SET_HP_FORCE_ON) : 
      serialOpertion = sendRequest(SERIAL_OPERATION::SET_HP_FORCE_OFF);
  }

  //working_watt
  if (!doc["working_watt"].isNull() && jsonAsString(doc["working_watt"]) != "" ) 
  {
    serialOpertion = sendRequest(SERIAL_OPERATION::SET_WORKING_WATT, jsonAsString(doc["working_watt"]).toDouble());  
  }

  //EEV_max_pulse_open
  if (!doc["eev_max_pulse_open"].isNull() && jsonAsString(doc["eev_max_pulse_open"]) != "" ) 
  {
    serialOpertion = sendRequest(SERIAL_OPERATION::SET_EEV_MAXPULSES_OPEN, jsonAsString(doc["eev_max_pulse_open"]).toDouble());  
  }

  
  String data = "";
  serializeJsonPretty(doc, data);
  server.send(200, "application/json", data);
}


void putHpDataToCloud(void) {
  String response = putDataToCloud("hp/add", jsonDocument);
  if (response == "" ) {
    return;
  } 

  JsonDocument ddd;
  DeserializationError error = deserializeJson(ddd, response);
  if (error) {
    printSerial("Bad JSON");
    delay(300);
    return;
  }  
  printSerial(ddd["operation"]);
  operationExecute(ddd["operation"]);
}

String putDataToCloud(String path, JsonDocument data) {

  if (WiFi.status() != WL_CONNECTED) {
      printSerial("Brak połączenia WiFi");
      delay(100);
      return "";
  }

  http.begin("https://chpc-web.onrender.com/api/" + path);  // Adres serwera lub API
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Cache-Control", "no-cache");

  String payload;
  serializeJsonPretty(data, payload);
  
  http.setTimeout(1000);
  int httpCode = http.POST(payload);
  http.end();

  if (httpCode < 0 ) {
    char buffer[128];  // bufor na wynik formatowania
    sprintf(buffer, "Błąd żądania: %s\n", http.errorToString(httpCode).c_str());
    printSerial(buffer);
    return "";
  }   
  return http.getString();
}

/*
{"Tbe":"23.6","Tae":"23.3","Tco":"23.7","Tho":"23.4","Ttarget":"24.8","Tsump":"23.9","EEV_dt":"0.0","Tcwu":"25.0","Tmax":"18.5","Tmin":"13.0","Tcwu_max":"26.0","Tcwu_min":"23.0","Watts":"72","EEV":"2.0","EEV_pos":"50","HCS":0,"CCS":0,"HPS":0,"F":0,"CWUS":0,"CWU":1,"CO":1}
*/