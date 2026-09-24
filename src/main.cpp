#include <Arduino.h>
#include <Preferences.h>
#include <cloud_client.hpp>
#include <config_portal.hpp>
#include <device_config.hpp>
#include <device_io.hpp>
#include <hardware_config.hpp>
#include <heat_pump_data_processor.hpp>
#include <json_converters.hpp>
#include <operation_controller.hpp>
#include <operation_parser.hpp>
#include <pv_data_processor.hpp>
#include <serial_bus.hpp>
#include <telemetry.hpp>

constexpr int64_t HP_FORCE_ON = 2000;
constexpr unsigned long MILLIS_REFRESH_ACTIVE = 10000;
constexpr unsigned long MILLIS_REFRESH_IDLE = 30000;
unsigned long refreshInterval = MILLIS_REFRESH_IDLE;
constexpr unsigned long TIME_SYNC_INTERVAL = 6UL * 60UL * 60UL * 1000UL;
constexpr unsigned long TIME_SYNC_RETRY_INTERVAL = 5UL * 60UL * 1000UL;
constexpr unsigned long BUTTON_DEBOUNCE_MS = 50;
constexpr unsigned long MODE_CHANGE_DELAY_MS = 5000;
constexpr const char *CONTROLLER_MODE_KEY = "mode";



// Everything below belongs to this translation unit alone.
namespace {
RTC_DS3231 rtc;
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS_PIN, TFT_DC_PIN, TFT_MOSI_PIN,
  TFT_CLOCK_PIN, TFT_RESET_PIN);
Telemetry telemetry;
DateTime rtcTime;
PV pv;
SerialBus serialBus(Serial);
OperationController operationController(serialBus, HP_FORCE_ON);
CloudClient cloudClient;
HeatPumpDataProcessor heatPumpDataProcessor;
PvDataProcessor pvDataProcessor;
Preferences devicePreferences;

// temporary variables
unsigned long lastRefreshAt = -1;
unsigned long lastTimeSyncAt = 0;
unsigned long timeSyncInterval = TIME_SYNC_RETRY_INTERVAL;
uint32_t scheduledReadCount = 0;
uint32_t pvCrcErrors = 0;
uint32_t operationValidationErrors = 0;
uint32_t cloudResponseParseErrors = 0;
uint32_t hpJsonErrors = 0;
uint32_t pvFrameErrors = 0;
bool cloudPostPending = false;
bool buttonStableState = false;
bool buttonCandidateState = false;
unsigned long buttonCandidateSince = 0;
bool pendingControllerMode = false;
ControllerMode requestedControllerMode = ControllerMode::CLOUD;
unsigned long requestedControllerModeAt = 0;
bool pvFollowUpPending = false;
}

// global functions
void respondToSerialRequest(char operation);
void processSerialInput();
void postTelemetryToCloud();
void applyServerOperation(JsonObjectConst operation);
void scheduleNextDeviceRead();
void applyControllerOutputs(void);
void processControlButton();
void applyPendingControllerMode();
ControllerMode nextControllerMode(ControllerMode currentMode);
ControllerMode loadControllerMode();
void saveControllerMode(ControllerMode mode);

// main
void setup()
{
  serialBus.begin(9600);

  Wire.begin();
  rtc.begin();

  pinMode(RELAY_HP_CWU_PIN, OUTPUT);
  pinMode(RELAY_HP_CO_PIN, OUTPUT);
  pinMode(POWER_PIN, OUTPUT);
  pinMode(CONTROL_BUTTON_PIN, INPUT);

  digitalWrite(POWER_PIN, HIGH);
  buttonStableState = digitalRead(CONTROL_BUTTON_PIN) == HIGH;
  buttonCandidateState = buttonStableState;
  loadDeviceConfig();
  bool timeSynchronized = initializeDevice(rtc, tft);
  operationController.setControllerMode(loadControllerMode());
  applyControllerOutputs();
  lastTimeSyncAt = millis();
  timeSyncInterval = timeSynchronized
    ? TIME_SYNC_INTERVAL : TIME_SYNC_RETRY_INTERVAL;

  beginConfigPortal(telemetry);
  cloudClient.begin();
}

void loop()
{
  processControlButton();
  applyPendingControllerMode();
  serialBus.tick();
  operationController.tick();
  applyControllerOutputs();
  processSerialInput();
  serialBus.tick();
  cloudClient.tick();
  handleConfigPortal();

  if (pvFollowUpPending) {
    pvFollowUpPending =
      !serialBus.enqueueFollowUp(SERIAL_OPERATION::GET_PV_DATA_2);
  }

  if (cloudClient.takeOperationRequest()) cloudPostPending = true;

  if (millis() - lastTimeSyncAt >= timeSyncInterval && serialBus.isIdle()) {
    lastTimeSyncAt = millis();
    timeSyncInterval = synchronizeClock(rtc)
      ? TIME_SYNC_INTERVAL : TIME_SYNC_RETRY_INTERVAL;
  }

  if (cloudPostPending && serialBus.isIdle()) {
    cloudPostPending = false;
    postTelemetryToCloud();
  }

  if (lastRefreshAt == static_cast<unsigned long>(-1)
    || millis() - lastRefreshAt > refreshInterval)
  {
    lastRefreshAt = millis();
  
    rtcTime = rtc.now(); // Get current time from RTC
    const DeviceSettings &prefs = operationController.preferences();
    bool coPump = operationController.coRelay();
    bool cwuPump = operationController.cwuRelay();

    telemetry.updateSnapshot(rtcTime, coPump, cwuPump, pv,
      operationController.controllerMode(), prefs);
    telemetry.updateSerialDiagnostics(
      serialBus.queueOverflowCount(), serialBus.readTimeoutCount(),
      serialBus.receiveOverflowCount(), pvCrcErrors, hpJsonErrors, pvFrameErrors);
    telemetry.updateCloudDiagnostics(
      cloudClient.lastHttpStatus(), cloudClient.requestErrorCount(),
      cloudClient.webSocketDisconnectCount(), cloudResponseParseErrors);
    telemetry.updateOperationDiagnostics(operationValidationErrors,
      operationController.preferenceValidationErrorCount());

    refreshInterval = telemetry.heatPumpRunning()
      ? MILLIS_REFRESH_ACTIVE : MILLIS_REFRESH_IDLE;

    // The dashboard would wipe the mode the button is currently selecting.
    if (!pendingControllerMode) {
      renderDashboard(tft, coPump, rtcTime, telemetry.document(),
        operationController.controllerMode(), prefs.workMode, pv, prefs);
    }

    cloudPostPending = true;
    scheduleNextDeviceRead();
  }
}

ControllerMode nextControllerMode(ControllerMode currentMode)
{
  switch (currentMode) {
    case ControllerMode::OFF: return ControllerMode::CLOUD;
    case ControllerMode::CLOUD: return ControllerMode::MANUAL_CO;
    case ControllerMode::MANUAL_CO: return ControllerMode::MANUAL_CWU;
    case ControllerMode::MANUAL_CWU: return ControllerMode::OFF;
  }
  return ControllerMode::CLOUD;
}

void processControlButton()
{
  bool pressed = digitalRead(CONTROL_BUTTON_PIN) == HIGH;
  unsigned long now = millis();

  if (pressed != buttonCandidateState) {
    buttonCandidateState = pressed;
    buttonCandidateSince = now;
    return;
  }

  if (buttonCandidateState == buttonStableState
    || now - buttonCandidateSince < BUTTON_DEBOUNCE_MS) return;

  buttonStableState = buttonCandidateState;
  if (!buttonStableState) return;

  ControllerMode baseMode = pendingControllerMode
    ? requestedControllerMode : operationController.controllerMode();
  requestedControllerMode = nextControllerMode(baseMode);
  requestedControllerModeAt = now;
  pendingControllerMode = true;

  // Show the candidate straight away. Every further press within
  // MODE_CHANGE_DELAY_MS moves on to the next mode and restarts the delay, so
  // whatever is on screen when it expires is the mode that gets applied.
  displayControllerMode(tft, requestedControllerMode,
    operationController.preferences().workMode);
}

void applyPendingControllerMode()
{
  if (!pendingControllerMode
    || millis() - requestedControllerModeAt < MODE_CHANGE_DELAY_MS) return;

  pendingControllerMode = false;
  serialBus.cancelControlCommands();
  operationController.setControllerMode(requestedControllerMode);
  saveControllerMode(requestedControllerMode);
  applyControllerOutputs();
  cloudPostPending = true;
}

// Only the controller mode is decided locally and has to survive a restart.
// Temperatures and the work mode come from the server, so they stay at the
// DeviceSettings defaults until the first operation arrives. Storing a typed
// key instead of a raw struct keeps the stored data readable after any change
// to DeviceSettings.
ControllerMode loadControllerMode()
{
  devicePreferences.begin(PREFERENCES_NAMESPACE, true);
  uint8_t stored = devicePreferences.getUChar(CONTROLLER_MODE_KEY,
    static_cast<uint8_t>(ControllerMode::CLOUD));
  devicePreferences.end();

  return stored <= static_cast<uint8_t>(ControllerMode::MANUAL_CWU)
    ? static_cast<ControllerMode>(stored) : ControllerMode::CLOUD;
}

void saveControllerMode(ControllerMode mode)
{
  devicePreferences.begin(PREFERENCES_NAMESPACE, false);
  devicePreferences.putUChar(CONTROLLER_MODE_KEY, static_cast<uint8_t>(mode));
  devicePreferences.end();
}

void scheduleNextDeviceRead()
{
  bool queued;
  if (scheduledReadCount % 10 == 0) {
    queued = serialBus.enqueue(SERIAL_OPERATION::GET_PV_DATA_1);
    if (queued) {
      pvDataProcessor.reset();
      pvFollowUpPending = false;
    }
  } else {
    queued = serialBus.enqueue(SERIAL_OPERATION::GET_HP_DATA);
  }

  if (queued) scheduledReadCount++;
}

void processSerialInput()
{
  static uint8_t inData[SerialBus::RX_BUFFER_SIZE];
  size_t length = serialBus.readFrame(inData, sizeof(inData));
  if (length == 0) return;

  if (length >= 4 && inData[0] == CONTROLLER_DEVICE_ID && inData[3] == 0xFF) {
    respondToSerialRequest(static_cast<char>(inData[1]));
    return;
  }

  PendingRead pendingRead = serialBus.pendingRead();
  bool pvPending = pendingRead == PendingRead::PV_PART_1
    || pendingRead == PendingRead::PV_PART_2;

  // A Modbus exception response means the inverter rejected the request.
  // Without this branch it would only ever surface as a read timeout.
  if (pvPending && length >= 2 && inData[0] == PV_DEVICE_ID
    && inData[1] == 0x83) {
    pvFrameErrors++;
    serialBus.completeRead();
    return;
  }

  if (pvPending && length >= 2 && inData[0] == PV_DEVICE_ID
    && inData[1] == 0x03) {
    if (!serialBus.validateModbusFrame(inData, length)) {
      pvCrcErrors++;
      serialBus.completeRead();
      return;
    }
    bool valid = pvDataProcessor.appendFrame(inData, length);
    serialBus.completeRead();

    if (!valid) {
      pvFrameErrors++;
      return;
    }
    if (pendingRead == PendingRead::PV_PART_1) {
      // A refused follow-up leaves the PV reading half finished, so it is
      // retried instead of waiting for the next full cycle.
      pvFollowUpPending =
        !serialBus.enqueueFollowUp(SERIAL_OPERATION::GET_PV_DATA_2);
    } else {
      if (!pvDataProcessor.complete(pv, HP_FORCE_ON)) {
        pvFrameErrors++;
        return;
      }
      telemetry.updatePv(pv);
      operationController.updatePv(pv);
      applyControllerOutputs();
    }
    return;
  }

  if (pendingRead == PendingRead::HP) {
    serialBus.completeRead();
    HeatPumpDataUpdate update;
    if (!heatPumpDataProcessor.processFrame(inData, length, update)) {
      hpJsonErrors++;
    } else {
      telemetry.updateHeatPump(update);
    }
  }
}

void respondToSerialRequest(char operation)
{
  String data = "";
  switch (operation)
  {
  case 0x01:
    serializeJson(telemetry.document(), data);
    break;
  case 0x02: {
    JsonDocument settingsDocument;
    settingsDocument.set(operationController.preferences());
    settingsDocument["controller_mode"] = operationController.controllerMode();
    serializeJson(settingsDocument, data);
    break;
  }
  case 0x03:
    // NOP
    break;
  default:
    JsonDocument doc;
    doc["error"] = 2;
    serializeJson(doc, data);
    break;
  }
  writeSerialResponse(data);
}

// TODO(server): Scheduler must perform the MANUAL -> AUTO transition.
// This firmware applies only work_mode changes received from the server.
void applyServerOperation(JsonObjectConst operation)
{
  if (operationController.controllerMode() != ControllerMode::CLOUD) return;
  if (operation.isNull() || operation.size() == 0) return;

  OperationParseResult parsed = parseServerOperation(operation);
  operationValidationErrors += parsed.invalidValues;
  operationController.applyServerPatch(parsed.state);
  applyControllerOutputs();
}

void applyControllerOutputs()
{
  bool modeChanged = operationController.takeModeChanged();
  bool relayChanged = operationController.takeRelayChanged();
  if (!modeChanged && !relayChanged) return;

  const DeviceSettings &prefs = operationController.preferences();

  if (relayChanged) {
    writeRelayOutput(tft, RELAY_HP_CO_PIN, operationController.coRelay());
    writeRelayOutput(tft, RELAY_HP_CWU_PIN, operationController.cwuRelay());
  }

  displayControllerMode(tft,
    operationController.controllerMode(), prefs.workMode);

  telemetry.updateControllerState(operationController.coRelay(),
    operationController.cwuRelay(), operationController.controllerMode(), prefs);
}


void postTelemetryToCloud() {
  JsonObjectConst heatPump = telemetry.document()["HP"].as<JsonObjectConst>();
  if (heatPump.isNull() || heatPump.size() == 0) {
    return;
  }
  
  String response = cloudClient.post("hp/add", telemetry.document());
  if (response == "" ) {
    return;
  } 

  JsonDocument responseDocument;
  DeserializationError error = deserializeJson(responseDocument, response);
  if (error) {
    cloudResponseParseErrors++;
    return;
  }  
  
  applyServerOperation(responseDocument["operation"].as<JsonObjectConst>());
}
