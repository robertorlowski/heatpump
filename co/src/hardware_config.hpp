#pragma once

#include <cstdint>

constexpr uint8_t CONTROLLER_DEVICE_ID = 0x10;
constexpr uint8_t PV_DEVICE_ID = 0x69;

// Hoymiles DTU Modbus map: the microinverter port block starts at 0x1000 and
// every port occupies 20 registers (40 bytes). One request covers five ports,
// two requests cover the whole installation. The request encoder and the
// response parser must derive their addresses from these same constants.
constexpr uint16_t PV_FIRST_REGISTER = 0x1000;
constexpr uint8_t PV_REGISTERS_PER_DEVICE = 20;
constexpr uint8_t PV_BYTES_PER_DEVICE = PV_REGISTERS_PER_DEVICE * 2;
constexpr uint8_t PV_DEVICES_PER_REQUEST = 5;
constexpr uint8_t PV_REQUEST_COUNT = 2;

// ESP32-WROOM
constexpr uint8_t TFT_DC_PIN = 12;
constexpr uint8_t TFT_CS_PIN = 13;
constexpr uint8_t TFT_MOSI_PIN = 14;
constexpr uint8_t TFT_CLOCK_PIN = 27;
constexpr uint8_t TFT_RESET_PIN = 0;
constexpr uint8_t RELAY_HP_CWU_PIN = 25;
constexpr uint8_t RELAY_HP_CO_PIN = 26;
constexpr uint8_t POWER_PIN = 18;
constexpr uint8_t CONTROL_BUTTON_PIN = 5;
