#pragma once

#include <domain_types.hpp>

class CommandSink {
public:
  virtual ~CommandSink() = default;
  virtual bool enqueue(SERIAL_OPERATION operation, double value = 0.0) = 0;
  virtual bool enqueuePriority(SERIAL_OPERATION operation, double value = 0.0) = 0;
};
