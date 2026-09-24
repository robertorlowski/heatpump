#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include <command_sink.hpp>

enum class PendingRead : uint8_t {
  NONE,
  HP,
  PV_PART_1,
  PV_PART_2
};

class SerialBus : public CommandSink {
public:
  // Sized to hold a whole inverter response; the frame buffer in main.cpp
  // must match, so both take it from here.
  static constexpr size_t RX_BUFFER_SIZE = 2048;

  explicit SerialBus(HardwareSerial &serial);

  void begin(uint32_t baud);
  void tick();

  bool enqueue(SERIAL_OPERATION operation, double value = 0.0) override;
  bool enqueuePriority(SERIAL_OPERATION operation, double value = 0.0) override;
  bool enqueueFollowUp(SERIAL_OPERATION operation, double value = 0.0);
  void cancelControlCommands();

  size_t readFrame(uint8_t *buffer, size_t capacity);
  bool validateModbusFrame(const uint8_t *buffer, size_t length) const;
  PendingRead pendingRead() const;
  bool isIdle() const;
  void completeRead();
  uint32_t queueOverflowCount() const;
  uint32_t readTimeoutCount() const;
  uint32_t receiveOverflowCount() const;

private:
  struct Command {
    SERIAL_OPERATION operation;
    double value;
  };

  enum class QueueClass : uint8_t {
    SAFETY,
    FOLLOW_UP,
    NORMAL
  };

  static constexpr size_t QUEUE_SIZE = 32;
  static constexpr unsigned long COMMAND_GAP_MS = 500;
  static constexpr unsigned long READ_TIMEOUT_MS = 3000;
  static constexpr unsigned long FRAME_GAP_MS = 5;

  HardwareSerial &serial;
  Command queue[QUEUE_SIZE];
  size_t queueCount = 0;
  size_t safetyCount = 0;
  size_t followUpCount = 0;
  uint8_t receiveBuffer[RX_BUFFER_SIZE]{};
  size_t receiveLength = 0;
  unsigned long lastWriteAt = 0;
  unsigned long pendingSince = 0;
  unsigned long lastByteAt = 0;
  bool hasWritten = false;
  bool discardingReceiveFrame = false;
  PendingRead pending = PendingRead::NONE;
  uint32_t queueOverflows = 0;
  uint32_t readTimeouts = 0;
  uint32_t receiveOverflows = 0;

  bool enqueueCommand(const Command &command, QueueClass queueClass);
  bool popFront(Command &command);
  void removeAt(size_t index);
  int commandKey(SERIAL_OPERATION operation) const;
  void writeCommand(const Command &command);
  PendingRead readTypeFor(SERIAL_OPERATION operation) const;
  bool isReadOperation(SERIAL_OPERATION operation) const;
};
