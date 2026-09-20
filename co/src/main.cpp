#include <Arduino.h>
#include <utils.hpp>
#include <cloud_client.hpp>
#include <cop_estimator.hpp>
#include <operation_controller.hpp>
#include <operation_parser.hpp>
#include <serial_bus.hpp>

constexpr uint8_t PV_DEVICE_COUNT = 5;
constexpr int64_t HP_FORCE_ON = 2000;
constexpr unsigned long MILLIS_REFRESH_ACTIVE = 10000;
constexpr unsigned long MILLIS_REFRESH_IDLE = 30000;
unsigned long refreshInterval = MILLIS_REFRESH_IDLE;
constexpr unsigned long TIME_SYNC_INTERVAL = 6UL * 60UL * 60UL * 1000UL;
constexpr unsigned long TIME_SYNC_RETRY_INTERVAL = 5UL * 60UL * 1000UL;



// global variables
RTC_DS3231 rtc;
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_CLK, TFT_RST);
JsonDocument jsonDocument;
DateTime rtcTime;
PV pv;
SerialBus serialBus(Serial);
OperationController operationController(serialBus, HP_FORCE_ON);
CloudClient cloudClient;
CopEstimator copEstimator;

// temporary variables
unsigned long millisRefreshAt = -1;
unsigned long millisTimeSyncAt = 0;
unsigned long timeSyncInterval = TIME_SYNC_RETRY_INTERVAL;
uint32_t readCounter = 0;
uint32_t pvCrcErrors = 0;
uint32_t operationValidationErrors = 0;
uint32_t cloudResponseParseErrors = 0;
uint32_t hpJsonErrors = 0;
uint32_t pvFrameErrors = 0;
bool cloudPostPending = false;

// global functions
void sendDataToSerial(char operation);
uint32_t getPvData(uint8_t pvNumber, const uint8_t *data,
  uint8_t startByte, uint8_t byteCount);
bool collectDataFromPV(const uint8_t *inData, size_t length);
void collectDataFromSerial();
void putHpDataToCloud(void);
void operationExecute(JsonDocument doc);
void getDataFromHpPv(void);
void calculateCOP(JsonObject hp);
void applyControllerOutputs(void);

// main
void setup()
{
  serialBus.begin(9600, SERIAL_BUFFER);

  Wire.begin();
  rtc.begin();

  pinMode(RELAY_HP_CWU, OUTPUT);
  pinMode(RELAY_HP_CO, OUTPUT);
  pinMode(PWR, OUTPUT);

  digitalWrite(PWR, HIGH);
  bool timeSynchronized = initialize(rtc, tft);
  millisTimeSyncAt = millis();
  timeSyncInterval = timeSynchronized
    ? TIME_SYNC_INTERVAL : TIME_SYNC_RETRY_INTERVAL;

  jsonDocument["HP"].to<JsonObject>();
  jsonDocument["PV"].to<JsonObject>();

  cloudClient.begin();
}

void loop()
{
  serialBus.tick();
  operationController.tick();
  collectDataFromSerial();
  serialBus.tick();
  cloudClient.tick();

  if (cloudClient.takeOperationRequest()) cloudPostPending = true;

  if (millis() - millisTimeSyncAt >= timeSyncInterval && serialBus.isIdle()) {
    millisTimeSyncAt = millis();
    timeSyncInterval = synchronizeRtc(rtc)
      ? TIME_SYNC_INTERVAL : TIME_SYNC_RETRY_INTERVAL;
  }

  if (cloudPostPending && serialBus.isIdle()) {
    cloudPostPending = false;
    putHpDataToCloud();
  }

  if (millisRefreshAt == static_cast<unsigned long>(-1)
    || millis() - millisRefreshAt > refreshInterval)
  {
    millisRefreshAt = millis();
  
    rtcTime = rtc.now(); // Get current time from RTC
    const HpPreferences &prefs = operationController.preferences();
    bool co_pomp = operationController.coRelay();
    bool cwu_pomp = operationController.cwuRelay();
    
    jsonDocument["time"] = rtcTime;
    jsonDocument["co_pomp"] = co_pomp;
    jsonDocument["cwu_pomp"] = cwu_pomp; 
    jsonDocument["pv_power"] = pv.pv_power;
    jsonDocument["work_mode"] = prefs.workMode;
    jsonDocument["co_min"] = prefs.coMin;
    jsonDocument["co_max"] = prefs.coMax;
    jsonDocument["cwu_min"] = prefs.cwuMin;
    jsonDocument["cwu_max"] = prefs.cwuMax;
    jsonDocument["serial_queue_overflow"] = serialBus.queueOverflowCount();
    jsonDocument["serial_read_timeout"] = serialBus.readTimeoutCount();
    jsonDocument["serial_receive_overflow"] = serialBus.receiveOverflowCount();
    jsonDocument["pv_crc_error"] = pvCrcErrors;
    jsonDocument["cloud_http_status"] = cloudClient.lastHttpStatus();
    jsonDocument["cloud_request_error"] = cloudClient.requestErrorCount();
    jsonDocument["websocket_disconnect"] = cloudClient.webSocketDisconnectCount();
    jsonDocument["operation_validation_error"] = operationValidationErrors;
    jsonDocument["cloud_response_parse_error"] = cloudResponseParseErrors;
    jsonDocument["hp_json_error"] = hpJsonErrors;
    jsonDocument["pv_frame_error"] = pvFrameErrors;
    jsonDocument["preference_validation_error"] =
      operationController.preferenceValidationErrorCount();

    JsonObject hp = jsonDocument["HP"].as<JsonObject>();

    refreshInterval = jsonAsInt(hp["HPS"]) > 0
      ? MILLIS_REFRESH_ACTIVE : MILLIS_REFRESH_IDLE;

    // print ALL
    PrintAll(tft, co_pomp, cwu_pomp, -1, rtcTime, jsonDocument, prefs.workMode, pv, prefs);

    cloudPostPending = true;
    getDataFromHpPv();
  }
}

void calculateCOP(JsonObject hp) 
{
  if (hp.isNull() || hp["HPS"].isNull() || hp["Tho"].isNull()
    || hp["Ttarget"].isNull()) return;

  const bool running = jsonAsInt(hp["HPS"]) > 0;
  const double topTemperature = jsonAsString(hp["Tho"]).toDouble();
  const double middleTemperature = jsonAsString(hp["Ttarget"]).toDouble();
  const double electricalEnergyWh = hp["lt_pow"].isNull()
    ? 0.0 : jsonAsString(hp["lt_pow"]).toDouble();
  const uint32_t cycleDurationSeconds = hp["lt_hp_on"].isNull()
    ? 0 : static_cast<uint32_t>(jsonAsString(hp["lt_hp_on"]).toDouble());

  CopCycleEvent event = copEstimator.update(running, topTemperature,
    middleTemperature, electricalEnergyWh, cycleDurationSeconds);

  if (event == CopCycleEvent::STARTED) {
    jsonDocument["t_min"] = middleTemperature;
    jsonDocument["t_max"] = middleTemperature;
    jsonDocument["cop"].clear();
    jsonDocument["cop_min"].clear();
    jsonDocument["cop_max"].clear();
    jsonDocument["cop_bottom_start"].clear();
    return;
  }

  if (copEstimator.cycleActive()) {
    jsonDocument["t_max"] = copEstimator.currentMiddleTemperature();
    return;
  }

  if (event != CopCycleEvent::COMPLETED) return;

  const CopEstimate &estimate = copEstimator.estimate();
  jsonDocument["t_min"] = estimate.startMiddleTemperature;
  jsonDocument["t_max"] = estimate.endMiddleTemperature;
  jsonDocument["cop_bottom_start"] = estimate.startBottomTemperature;

  if (!estimate.valid) {
    jsonDocument["cop"].clear();
    jsonDocument["cop_min"].clear();
    jsonDocument["cop_max"].clear();
    return;
  }

  jsonDocument["cop_min"] = round(estimate.minimum * 100.0) / 100.0;
  jsonDocument["cop_max"] = round(estimate.maximum * 100.0) / 100.0;
  jsonDocument["cop"] = round(estimate.estimated * 100.0) / 100.0;
}

void getDataFromHpPv(void) 
{
  bool queued;
  if (readCounter % 10 == 0) {
    queued = serialBus.enqueue(SERIAL_OPERATION::GET_PV_DATA_1);
    if (queued) {
      pv.total_power = 0;
      pv.total_prod = 0;
      pv.total_prod_today = 0;
      pv.temperature = 0;
    }
  } else {
    queued = serialBus.enqueue(SERIAL_OPERATION::GET_HP_DATA);
  }

  if (queued) readCounter++;
}

void collectDataFromSerial()
{
  uint8_t inData[SERIAL_BUFFER];
  size_t length = serialBus.readFrame(inData, sizeof(inData));
  if (length == 0) return;

  if (length >= 4 && inData[0] == static_cast<uint8_t>(devID) && inData[3] == 0xFF) {
    sendDataToSerial(static_cast<char>(inData[1]));
    return;
  }

  PendingRead pendingRead = serialBus.pendingRead();
  if ((pendingRead == PendingRead::PV_PART_1 || pendingRead == PendingRead::PV_PART_2)
    && length >= 2 && inData[0] == PV_DEVICE_ID && inData[1] == 0x03) {
    if (!serialBus.validateModbusFrame(inData, length)) {
      pvCrcErrors++;
      serialBus.completeRead();
      return;
    }
    bool valid = collectDataFromPV(inData, length);
    serialBus.completeRead();

    if (!valid) {
      pvFrameErrors++;
      return;
    }
    if (pendingRead == PendingRead::PV_PART_1) {
      serialBus.enqueueFollowUp(SERIAL_OPERATION::GET_PV_DATA_2);
    } else {
      pv.pv_power = pv.total_power >= HP_FORCE_ON;
      jsonDocument["pv_power"] = pv.pv_power;
      jsonDocument["PV"] = pv;
      operationController.updatePv(pv);
      applyControllerOutputs();
    }
    return;
  }

  if (pendingRead == PendingRead::HP) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(
      doc, reinterpret_cast<const char *>(inData), length);
    serialBus.completeRead();
    if (!error) {
      jsonDocument["HP"] = doc;
      calculateCOP(jsonDocument["HP"].as<JsonObject>());
    } else {
      hpJsonErrors++;
    }
  }
}

bool collectDataFromPV(const uint8_t *inData, size_t length)
{
  constexpr size_t requiredLength = 31 + (PV_DEVICE_COUNT - 1) * 40;
  if (length < requiredLength) return false;

  for (int i = 0; i < PV_DEVICE_COUNT; i++)
  {
    pv.total_power += getPvData(i, inData, 19, 2) / 10;
    pv.total_prod_today += getPvData(i, inData, 21, 2);
    pv.total_prod += getPvData(i, inData, 23, 4);
    pv.temperature = getPvData(i, inData, 27, 2) / 10.0f;
  }
  return true;
}

void sendDataToSerial(char operation)
{
  String data = "";
  switch (operation)
  {
  case 0x01:
    serializeJsonPretty(jsonDocument, data);
    break;
  case 0x02: {
    JsonDocument settingsDocument;
    settingsDocument.set(operationController.preferences());
    serializeJsonPretty(settingsDocument, data);
    break;
  }
  case 0x03:
    // NOP
    break;
  default:
    JsonDocument doc;
    doc["error"] = 2;
    serializeJsonPretty(doc, data);
    break;
  }
  sendSerialText(data);
}

uint32_t getPvData(uint8_t pvNumber, const uint8_t *data,
  uint8_t startByte, uint8_t byteCount)
{
  uint32_t ret = 0;
  for (int i = 0; i < byteCount; i++) {
    ret = (ret << 8) | data[startByte + i + pvNumber * 40];
  }

  return ret;
}

// TODO(server): Scheduler must perform the MANUAL -> AUTO transition.
// This firmware applies only work_mode changes received from the server.
void operationExecute(JsonDocument ddd) {
  JsonObject doc = ddd.as<JsonObject>();
  if (doc.isNull() || doc.size() == 0) return;

  OperationParseResult parsed = parseServerOperation(doc);
  operationValidationErrors += parsed.invalidValues;
  operationController.applyServerPatch(parsed.state);
  applyControllerOutputs();
}

void applyControllerOutputs()
{
  const HpPreferences &prefs = operationController.preferences();
  bool modeChanged = operationController.takeModeChanged();
  bool relayChanged = operationController.takeRelayChanged();

  if (relayChanged) {
    digitalWriteA(tft, RELAY_HP_CO, operationController.coRelay());
    digitalWriteA(tft, RELAY_HP_CWU, operationController.cwuRelay());
  }

  if (modeChanged || relayChanged) PrintMode(tft, prefs.workMode);

  jsonDocument["co_pomp"] = operationController.coRelay();
  jsonDocument["cwu_pomp"] = operationController.cwuRelay();
  jsonDocument["work_mode"] = prefs.workMode;
}


void putHpDataToCloud(void) {
  if (jsonDocument.isNull() || jsonDocument["HP"].isNull()) {
    return;
  }
  
  String response = cloudClient.post("hp/add", jsonDocument);
  if (response == "" ) {
    return;
  } 

  JsonDocument ddd;
  DeserializationError error = deserializeJson(ddd, response);
  if (error) {
    cloudResponseParseErrors++;
    return;
  }  
  
  JsonObject operation = ddd["operation"].as<JsonObject>();
  if (!operation.isNull() && operation.size() > 0) {
    JsonDocument operationDocument;
    operationDocument.set(operation);
    operationExecute(operationDocument);
  }
}
