#include <Arduino.h>
#include <Preferences.h>
#include <access_point_policy.hpp>
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
#include <pv_telemetry.hpp>
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
constexpr unsigned long MODE_SCREEN_MS = 3000;
constexpr const char *CONTROLLER_MODE_KEY = "mode";
// PV has its own timer, independent of the heat pump refresh interval, and
// the first reading is taken right after start.
constexpr unsigned long PV_READ_INTERVAL_MS = 60UL * 1000UL;
// A reading the cloud did not accept is sent again at this pace until a newer
// one replaces it.
constexpr unsigned long PV_POST_RETRY_MS = 60UL * 1000UL;
// Allows several missed readings before the inverter temperature is treated
// as stale.
constexpr unsigned long PV_TEMPERATURE_MAX_AGE_MS = 5UL * 60UL * 1000UL;
// After the last command of a batch the pump is read this soon instead of on
// the next regular cycle, so a missed command is sent again within seconds
// and the cloud sees the new state right away.
// CHPC handles RS-485 frames at the start of every loop and its loop does
// not block in normal operation, so this is margin enough.
constexpr unsigned long READ_AFTER_COMMAND_MS = 3000;
// The server refreshes the IMGW reading every 10 min; older than this means
// the cloud has stopped sending it, so the screen shows "--".
constexpr unsigned long OUTDOOR_TEMPERATURE_MAX_AGE_MS = 30UL * 60UL * 1000UL;
// A pump that keeps rejecting a command would otherwise be read every few
// seconds, because each check sends the command again.
constexpr unsigned long READ_AFTER_COMMAND_MIN_INTERVAL_MS = 10000;



// Everything below belongs to this translation unit alone.
namespace {
RTC_DS3231 rtc;
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS_PIN, TFT_DC_PIN, TFT_MOSI_PIN,
  TFT_CLOCK_PIN, TFT_RESET_PIN);
Telemetry telemetry;
PvTelemetry pvTelemetry;
DateTime rtcTime;
PV pv;
SerialBus serialBus(Serial);
OperationController operationController(serialBus, HP_FORCE_ON);
CloudClient cloudClient;
AccessPointPolicy accessPointPolicy;
HeatPumpDataProcessor heatPumpDataProcessor;
PvDataProcessor pvDataProcessor;
Preferences devicePreferences;

// temporary variables
unsigned long lastRefreshAt = -1;
unsigned long lastTimeSyncAt = 0;
unsigned long timeSyncInterval = TIME_SYNC_RETRY_INTERVAL;
unsigned long lastPvReadAt = -1;
bool pvPostPending = false;
bool pvPostRetryWait = false;
unsigned long lastPvPostAt = 0;
uint32_t pvCrcErrors = 0;
uint32_t operationValidationErrors = 0;
uint32_t cloudResponseParseErrors = 0;
uint32_t hpJsonErrors = 0;
uint32_t pvFrameErrors = 0;
bool cloudPostPending = false;
bool pvReceived = false;
unsigned long pvReceivedAt = 0;
bool buttonStableState = false;
bool buttonCandidateState = false;
unsigned long buttonCandidateSince = 0;
bool pendingControllerMode = false;
ControllerMode requestedControllerMode = ControllerMode::CLOUD;
unsigned long requestedControllerModeAt = 0;
bool modeScreenShown = false;
unsigned long modeScreenAt = 0;
bool pvFollowUpPending = false;
bool hpReadOutstanding = false;
bool readAfterCommandPending = false;
unsigned long lastControlCommandAt = 0;
bool readAfterCommandDone = false;
unsigned long lastReadAfterCommandAt = 0;
bool postAfterHpRead = false;
bool outdoorReceived = false;
float outdoorTemperature = 0.0f;
unsigned long outdoorReceivedAt = 0;
}

// global functions
void respondToSerialRequest(char operation);
void processSerialInput();
void reportHeatPumpState(JsonObjectConst hp);
void postTelemetryToCloud();
void postPvTelemetryToCloud();
void schedulePvRead();
void refreshTelemetry();
void applyServerOperation(JsonObjectConst operation);
void scheduleNextDeviceRead();
void applyControllerOutputs(void);
void processControlButton();
void applyPendingControllerMode();
void holdModeScreen();
void showDashboard();
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

  beginConfigPortal(telemetry, pvTelemetry);
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

  setAccessPointEnabled(accessPointPolicy.update(millis(), stationOnline(),
    cloudClient.lastRequestAnswered(), cloudClient.lastAnswerAt()));

  if (modeScreenShown && millis() - modeScreenAt >= MODE_SCREEN_MS) {
    modeScreenShown = false;
    if (!pendingControllerMode) showDashboard();
  }

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

  if (cloudClient.registrationDue() && serialBus.isIdle()) {
    cloudClient.registerDevice();
  }

  if (cloudPostPending && serialBus.isIdle()) {
    cloudPostPending = false;
    postTelemetryToCloud();
  }

  if (pvPostPending && serialBus.isIdle()
    && (!pvPostRetryWait || millis() - lastPvPostAt >= PV_POST_RETRY_MS)) {
    postPvTelemetryToCloud();
  }

  if (lastPvReadAt == static_cast<unsigned long>(-1)
    || millis() - lastPvReadAt >= PV_READ_INTERVAL_MS) {
    schedulePvRead();
  }

  if (serialBus.takeControlCommandWritten()) {
    readAfterCommandPending = true;
    lastControlCommandAt = millis();
  }

  // isIdle() also waits for the rest of the batch, so the delay counts from
  // the last command.
  if (readAfterCommandPending && serialBus.isIdle()
    && millis() - lastControlCommandAt >= READ_AFTER_COMMAND_MS
    && (!readAfterCommandDone
      || millis() - lastReadAfterCommandAt >= READ_AFTER_COMMAND_MIN_INTERVAL_MS))
  {
    readAfterCommandPending = false;
    readAfterCommandDone = true;
    lastReadAfterCommandAt = millis();
    // The regular cycle restarts here instead of reading again right away.
    lastRefreshAt = millis();
    postAfterHpRead = true;
    scheduleNextDeviceRead();
  }

  if (lastRefreshAt == static_cast<unsigned long>(-1)
    || millis() - lastRefreshAt > refreshInterval)
  {
    lastRefreshAt = millis();
    refreshTelemetry();

    // This read covers a command sent long enough ago.
    if (millis() - lastControlCommandAt >= READ_AFTER_COMMAND_MS)
      readAfterCommandPending = false;
    postAfterHpRead = false;
    cloudPostPending = true;
    scheduleNextDeviceRead();
  }
}

void refreshTelemetry()
{
  rtcTime = rtc.now(); // Get current time from RTC
  const DeviceSettings &prefs = operationController.preferences();
  bool coPump = operationController.coRelay();
  bool cwuPump = operationController.cwuRelay();

  telemetry.updateSnapshot(rtcTime, coPump, cwuPump,
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

  // The dashboard would wipe the mode the button is currently selecting or
  // the mode screen before its MODE_SCREEN_MS have passed.
  if (!pendingControllerMode && !modeScreenShown) showDashboard();
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

  // The first press only shows the current mode. Every further press within
  // MODE_CHANGE_DELAY_MS moves on to the next mode and restarts the delay, so
  // whatever is on screen when it expires is the mode that gets applied.
  requestedControllerMode = pendingControllerMode
    ? nextControllerMode(requestedControllerMode)
    : operationController.controllerMode();
  requestedControllerModeAt = now;
  pendingControllerMode = true;

  displayControllerMode(tft, requestedControllerMode,
    operationController.preferences().workMode);
}

void applyPendingControllerMode()
{
  if (!pendingControllerMode
    || millis() - requestedControllerModeAt < MODE_CHANGE_DELAY_MS) return;

  pendingControllerMode = false;

  // Only looked at, or cycled back round: the queued commands stay.
  if (requestedControllerMode == operationController.controllerMode()) {
    modeScreenShown = false;
    showDashboard();
    return;
  }

  serialBus.cancelControlCommands();
  operationController.setControllerMode(requestedControllerMode);
  saveControllerMode(requestedControllerMode);
  applyControllerOutputs();
  cloudPostPending = true;
}

// The mode screen stays for MODE_SCREEN_MS and then gives way to the
// dashboard, whatever the device read interval is.
void holdModeScreen()
{
  modeScreenShown = true;
  modeScreenAt = millis();
}

void showDashboard()
{
  const DeviceSettings &prefs = operationController.preferences();
  // At night the inverters sleep and the DTU keeps its last values (or stops
  // answering), so without production the temperature is hours old.
  bool pvTemperatureCurrent = pvReceived && pv.total_power > 0
    && millis() - pvReceivedAt < PV_TEMPERATURE_MAX_AGE_MS;
  bool outdoorCurrent = outdoorReceived
    && millis() - outdoorReceivedAt < OUTDOOR_TEMPERATURE_MAX_AGE_MS;
  renderDashboard(tft, rtcTime,
    telemetry.document(), operationController.controllerMode(),
    prefs.workMode, pv, pvTemperatureCurrent, prefs,
    outdoorTemperature, outdoorCurrent);
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
  if (!serialBus.enqueue(SERIAL_OPERATION::GET_HP_DATA)) return;
  // The previous read got no answer: CHPC is disconnected or restarting
  // and may have missed commands, so it gets the whole state once back.
  if (hpReadOutstanding) operationController.heatPumpLost();
  hpReadOutstanding = true;
}

// A refused request is retried on the next loop, so the timer restarts only
// once the first block is actually queued.
void schedulePvRead()
{
  if (!serialBus.enqueue(SERIAL_OPERATION::GET_PV_DATA_1)) return;
  lastPvReadAt = millis();
  pvDataProcessor.reset();
  pvFollowUpPending = false;
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
      pvReceived = true;
      pvReceivedAt = millis();
      pvTelemetry.update(rtc.now(), pv);
      pvPostPending = true;
      pvPostRetryWait = false;
      operationController.updatePv(pv);
      applyControllerOutputs();
    }
    return;
  }

  if (pendingRead == PendingRead::HP) {
    serialBus.completeRead();
    hpReadOutstanding = false;
    HeatPumpDataUpdate update;
    if (!heatPumpDataProcessor.processFrame(inData, length, update)) {
      hpJsonErrors++;
    } else {
      telemetry.updateHeatPump(update);
      reportHeatPumpState(update.hp.as<JsonObjectConst>());
      if (postAfterHpRead) {
        // The state after a command goes to the cloud at once.
        postAfterHpRead = false;
        refreshTelemetry();
        cloudPostPending = true;
      }
    }
  }
}

void reportHeatPumpState(JsonObjectConst hp)
{
  if (hp["CO"].isNull() || hp["F"].isNull() || hp["HPS"].isNull()) return;

  HeatPumpReport report;
  report.coOn = hp["CO"].as<int>() != 0;
  report.force = hp["F"].as<int>() != 0;
  report.running = hp["HPS"].as<int>() > 0;
  // CHPC sends numbers as strings; as<double>() parses them.
  report.hasTemperatures = !hp["Tmax"].isNull() && !hp["Tmin"].isNull();
  if (report.hasTemperatures) {
    report.setpoint = hp["Tmax"].as<double>();
    report.minimum = hp["Tmin"].as<double>();
  }
  operationController.updateHeatPumpReport(report);
  applyControllerOutputs();
}

void respondToSerialRequest(char operation)
{
  String data = "";
  switch (operation)
  {
  case 0x01: {
    // The bus reader still gets PV inside the telemetry, as before PV went
    // to the cloud on its own.
    JsonDocument response;
    response.set(telemetry.document());
    if (pvTelemetry.hasReading()) {
      response["PV"] = pvTelemetry.document();
      response["pv_power"] = pvTelemetry.document()["pv_power"];
    }
    serializeJson(response, data);
    break;
  }
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
  holdModeScreen();

  telemetry.updateControllerState(operationController.coRelay(),
    operationController.cwuRelay(), operationController.controllerMode(), prefs);
}


// Sent even without CHPC data: the server stores telemetry only when it holds
// HP.Ttarget, but always answers with the operation, so the controller gets
// its work mode while the pump is disconnected.
void postTelemetryToCloud() {
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
  
  // Outdoor temperature for the screen; the server leaves it out until it
  // has an IMGW reading.
  JsonVariantConst outdoor = responseDocument["t_out"];
  if (outdoor.is<float>()) {
    outdoorTemperature = outdoor.as<float>();
    outdoorReceived = true;
    outdoorReceivedAt = millis();
  }

  applyServerOperation(responseDocument["operation"].as<JsonObjectConst>());
}

// The answer carries no operation, so only its arrival matters: a rejected
// reading stays pending and is retried after PV_POST_RETRY_MS.
void postPvTelemetryToCloud()
{
  lastPvPostAt = millis();
  const bool accepted = cloudClient.post("pv/add", pvTelemetry.document()) != "";
  pvPostPending = !accepted;
  pvPostRetryWait = !accepted;
}
