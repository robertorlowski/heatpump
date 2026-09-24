#include <serial_bus.hpp>

#include <modbus_frame.hpp>

SerialBus::SerialBus(HardwareSerial &serial) : serial(serial)
{
}

void SerialBus::begin(uint32_t baud)
{
  serial.setRxBufferSize(RX_BUFFER_SIZE);
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
  return modbusCrcMatches(buffer, length);
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
  uint8_t buffer[MODBUS_FRAME_CAPACITY];
  const size_t length = encodeCommand(command.operation, command.value,
    buffer, sizeof(buffer));
  if (length == 0) return;

  serial.write(buffer, length);
  serial.flush();
}
