#include <serial_bus.hpp>

#include <FastCRC.h>
#include <hardware_config.hpp>

SerialBus::SerialBus(HardwareSerial &serial) : serial(serial)
{
}

void SerialBus::begin(uint32_t baud, size_t rxBufferSize)
{
  serial.setRxBufferSize(rxBufferSize);
  serial.begin(baud);
}

bool SerialBus::enqueue(SERIAL_OPERATION operation, double value)
{
  return enqueueCommand({operation, value}, QueueClass::NORMAL);
}

bool SerialBus::enqueuePriority(SERIAL_OPERATION operation, double value)
{
  return enqueueCommand({operation, value}, QueueClass::SAFETY);
}

bool SerialBus::enqueueFollowUp(SERIAL_OPERATION operation, double value)
{
  return enqueueCommand({operation, value}, QueueClass::FOLLOW_UP);
}

void SerialBus::cancelControlCommands()
{
  for (size_t index = 0; index < queueCount;) {
    if (isReadOperation(queue[index].operation)) {
      index++;
    } else {
      removeAt(index);
    }
  }
}

bool SerialBus::enqueueCommand(const Command &command, QueueClass queueClass)
{
  int key = commandKey(command.operation);
  for (size_t i = 0; i < queueCount; i++) {
    if (commandKey(queue[i].operation) == key) {
      removeAt(i);
      break;
    }
  }

  if (queueCount >= QUEUE_SIZE) {
    queueOverflows++;
    return false;
  }

  size_t index = queueClass == QueueClass::SAFETY ? safetyCount
    : queueClass == QueueClass::FOLLOW_UP ? safetyCount + followUpCount
    : queueCount;
  for (size_t i = queueCount; i > index; i--) queue[i] = queue[i - 1];
  queue[index] = command;
  queueCount++;
  if (queueClass == QueueClass::SAFETY) safetyCount++;
  if (queueClass == QueueClass::FOLLOW_UP) followUpCount++;
  return true;
}

bool SerialBus::popFront(Command &command)
{
  if (queueCount == 0) return false;
  command = queue[0];
  removeAt(0);
  return true;
}

void SerialBus::removeAt(size_t index)
{
  if (index >= queueCount) return;
  if (index < safetyCount) {
    safetyCount--;
  } else if (index < safetyCount + followUpCount) {
    followUpCount--;
  }
  for (size_t i = index; i + 1 < queueCount; i++) queue[i] = queue[i + 1];
  queueCount--;
}

int SerialBus::commandKey(SERIAL_OPERATION operation) const
{
  switch (operation) {
    case SET_HP_FORCE_ON:
    case SET_HP_FORCE_OFF: return 100;
    case SET_HP_CO_ON:
    case SET_HP_CO_OFF: return 101;
    case SET_SUMP_HEATER_ON:
    case SET_SUMP_HEATER_OFF: return 103;
    case SET_COLD_PUMP_ON:
    case SET_COLD_PUMP_OFF: return 104;
    case SET_HOT_PUMP_ON:
    case SET_HOT_PUMP_OFF: return 105;
    default: return 1000 + static_cast<int>(operation);
  }
}

void SerialBus::tick()
{
  unsigned long now = millis();

  if (pending != PendingRead::NONE) {
    if (now - pendingSince > READ_TIMEOUT_MS) {
      pending = PendingRead::NONE;
      receiveLength = 0;
      discardingReceiveFrame = false;
      readTimeouts++;
      lastWriteAt = now;
    }
    return;
  }

  if (queueCount == 0) return;
  if (hasWritten && now - lastWriteAt < COMMAND_GAP_MS) return;

  Command command;
  if (!popFront(command)) return;

  writeCommand(command);
  hasWritten = true;
  lastWriteAt = now;

  pending = readTypeFor(command.operation);
  if (pending != PendingRead::NONE) pendingSince = now;
}

size_t SerialBus::readFrame(uint8_t *buffer, size_t capacity)
{
  while (serial.available()) {
    uint8_t value = static_cast<uint8_t>(serial.read());
    lastByteAt = millis();
    if (discardingReceiveFrame) continue;

    if (receiveLength >= RX_BUFFER_SIZE) {
      receiveLength = 0;
      discardingReceiveFrame = true;
      receiveOverflows++;
      continue;
    }
    receiveBuffer[receiveLength++] = value;
  }

  if ((receiveLength == 0 && !discardingReceiveFrame)
    || millis() - lastByteAt < FRAME_GAP_MS) return 0;

  if (discardingReceiveFrame) {
    discardingReceiveFrame = false;
    receiveLength = 0;
    return 0;
  }

  size_t length = receiveLength;
  if (length > capacity) {
    length = capacity;
    receiveOverflows++;
  }
  memcpy(buffer, receiveBuffer, length);
  receiveLength = 0;
  return length;
}

bool SerialBus::validateModbusFrame(const uint8_t *buffer, size_t length) const
{
  if (length < 4) return false;
  FastCRC16 crc16;
  uint16_t crc = crc16.modbus(buffer, length - 2);
  bool highByteFirst = buffer[length - 2] == highByte(crc)
    && buffer[length - 1] == lowByte(crc);
  bool lowByteFirst = buffer[length - 2] == lowByte(crc)
    && buffer[length - 1] == highByte(crc);
  return highByteFirst || lowByteFirst;
}

PendingRead SerialBus::pendingRead() const
{
  return pending;
}

bool SerialBus::isIdle() const
{
  return pending == PendingRead::NONE
    && queueCount == 0
    && receiveLength == 0
    && !discardingReceiveFrame;
}

void SerialBus::completeRead()
{
  pending = PendingRead::NONE;
}

uint32_t SerialBus::queueOverflowCount() const
{
  return queueOverflows;
}

uint32_t SerialBus::readTimeoutCount() const
{
  return readTimeouts;
}

uint32_t SerialBus::receiveOverflowCount() const
{
  return receiveOverflows;
}

PendingRead SerialBus::readTypeFor(SERIAL_OPERATION operation) const
{
  switch (operation) {
    case SERIAL_OPERATION::GET_HP_DATA:
      return PendingRead::HP;
    case SERIAL_OPERATION::GET_PV_DATA_1:
      return PendingRead::PV_PART_1;
    case SERIAL_OPERATION::GET_PV_DATA_2:
      return PendingRead::PV_PART_2;
    default:
      return PendingRead::NONE;
  }
}

bool SerialBus::isReadOperation(SERIAL_OPERATION operation) const
{
  return operation == SERIAL_OPERATION::GET_HP_DATA
    || operation == SERIAL_OPERATION::GET_PV_DATA_1
    || operation == SERIAL_OPERATION::GET_PV_DATA_2;
}

void SerialBus::writeCommand(const Command &command)
{
  uint8_t buffer[10]{};
  size_t length = 5;

  switch (command.operation) {
    case GET_HP_DATA:
      buffer[0] = 0x41; buffer[1] = 0x01; buffer[4] = 0xFF;
      break;
    case GET_PV_DATA_1:
    case GET_PV_DATA_2: {
      FastCRC16 crc16;
      uint16_t start = command.operation == GET_PV_DATA_1
        ? 0x1000 : 0x1000 + 5 * 40;
      uint16_t count = command.operation == GET_PV_DATA_1 ? 0x0280 : 0x0320;
      buffer[0] = PV_DEVICE_ID;
      buffer[1] = 0x03;
      buffer[2] = highByte(start);
      buffer[3] = lowByte(start);
      buffer[4] = highByte(count);
      buffer[5] = lowByte(count);
      uint16_t crc = crc16.modbus(buffer, 6);
      buffer[6] = highByte(crc);
      buffer[7] = lowByte(crc);
      length = 8;
      break;
    }
    case SET_HP_FORCE_ON:
    case SET_HP_FORCE_OFF:
      buffer[0] = 0x41; buffer[1] = 0x03;
      buffer[2] = command.operation == SET_HP_FORCE_ON; buffer[4] = 0xFF;
      break;
    case SET_HP_CO_ON:
    case SET_HP_CO_OFF:
      buffer[0] = 0x41; buffer[1] = 0x0C;
      buffer[2] = command.operation == SET_HP_CO_ON; buffer[4] = 0xFF;
      break;
    case SET_SUMP_HEATER_ON:
    case SET_SUMP_HEATER_OFF:
      buffer[0] = 0x41; buffer[1] = 0x0B;
      buffer[2] = command.operation == SET_SUMP_HEATER_ON; buffer[4] = 0xFF;
      break;
    case SET_COLD_PUMP_ON:
    case SET_COLD_PUMP_OFF:
      buffer[0] = 0x41; buffer[1] = 0x0A;
      buffer[2] = command.operation == SET_COLD_PUMP_ON; buffer[4] = 0xFF;
      break;
    case SET_HOT_PUMP_ON:
    case SET_HOT_PUMP_OFF:
      buffer[0] = 0x41; buffer[1] = 0x09;
      buffer[2] = command.operation == SET_HOT_PUMP_ON; buffer[4] = 0xFF;
      break;
    case SET_T_SETPOINT_CO:
    case SET_T_DELTA_CO:
    case SET_EEV_SETPOINT: {
      buffer[0] = 0x41;
      buffer[1] = command.operation == SET_T_SETPOINT_CO ? 0x04
        : command.operation == SET_T_DELTA_CO ? 0x05 : 0x08;
      double bounded = command.value < 0 ? 0
        : command.value > 255.99 ? 255.99 : command.value;
      uint16_t scaled = static_cast<uint16_t>(round(bounded * 100.0));
      buffer[2] = scaled / 100;
      buffer[3] = scaled % 100;
      buffer[4] = 0xFF;
      break;
    }
    case SET_EEV_MAXPULSES_OPEN:
      buffer[0] = 0x41; buffer[1] = 0x0D;
      buffer[2] = static_cast<uint8_t>(round(command.value)); buffer[4] = 0xFF;
      break;
    case SET_WORKING_WATT: {
      buffer[0] = 0x41; buffer[1] = 0x0E;
      double bounded = command.value < 0 ? 0
        : command.value > 25599 ? 25599 : command.value;
      uint16_t value = static_cast<uint16_t>(round(bounded));
      buffer[2] = value / 100;
      buffer[3] = value % 100;
      buffer[4] = 0xFF;
      break;
    }
  }

  serial.write(buffer, length);
  serial.flush();
}
