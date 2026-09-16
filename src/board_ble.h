#pragma once
#include <Arduino.h>

// Board-specific GPIO numbers only. All BLE behavior lives in firmware_ble.inc.
// Matrix diode direction: COL ->| ROW.
// Encoder A/B/Push use internal pull-ups; common/push return go to GND.

#if defined(CHAINOSCPAD_TARGET_ESP32S3_BLE)
static constexpr uint8_t BOARD_ROW0 = 1;
static constexpr uint8_t BOARD_ROW1 = 2;
static constexpr uint8_t BOARD_ROW2 = 3;
static constexpr uint8_t BOARD_ROW3 = 4;
static constexpr uint8_t BOARD_COL0 = 5;
static constexpr uint8_t BOARD_COL1 = 6;
static constexpr uint8_t BOARD_COL2 = 43;
static constexpr uint8_t BOARD_ENCODER_A = 44;
static constexpr uint8_t BOARD_ENCODER_B = 7;
static constexpr uint8_t BOARD_ENCODER_PUSH = 8;
static constexpr uint8_t BOARD_STATUS_LED = 9;
#elif defined(CHAINOSCPAD_TARGET_ESP32C3_BLE)
static constexpr uint8_t BOARD_ROW0 = 2;
static constexpr uint8_t BOARD_ROW1 = 3;
static constexpr uint8_t BOARD_ROW2 = 4;
static constexpr uint8_t BOARD_ROW3 = 5;
static constexpr uint8_t BOARD_COL0 = 6;
static constexpr uint8_t BOARD_COL1 = 7;
static constexpr uint8_t BOARD_COL2 = 21;
static constexpr uint8_t BOARD_ENCODER_A = 20;
static constexpr uint8_t BOARD_ENCODER_B = 8;
static constexpr uint8_t BOARD_ENCODER_PUSH = 9;
static constexpr uint8_t BOARD_STATUS_LED = 10;
#elif defined(CHAINOSCPAD_TARGET_ESP32C5_BLE)
static constexpr uint8_t BOARD_ROW0 = 1;
static constexpr uint8_t BOARD_ROW1 = 0;
static constexpr uint8_t BOARD_ROW2 = 25;
static constexpr uint8_t BOARD_ROW3 = 7;
static constexpr uint8_t BOARD_COL0 = 23;
static constexpr uint8_t BOARD_COL1 = 24;
static constexpr uint8_t BOARD_COL2 = 11;
static constexpr uint8_t BOARD_ENCODER_A = 12;
static constexpr uint8_t BOARD_ENCODER_B = 8;
static constexpr uint8_t BOARD_ENCODER_PUSH = 9;
static constexpr uint8_t BOARD_STATUS_LED = 10;
#elif defined(CHAINOSCPAD_TARGET_ESP32C6_BLE)
static constexpr uint8_t BOARD_ROW0 = 0;
static constexpr uint8_t BOARD_ROW1 = 1;
static constexpr uint8_t BOARD_ROW2 = 2;
static constexpr uint8_t BOARD_ROW3 = 21;
static constexpr uint8_t BOARD_COL0 = 22;
static constexpr uint8_t BOARD_COL1 = 23;
static constexpr uint8_t BOARD_COL2 = 16;
static constexpr uint8_t BOARD_ENCODER_A = 17;
static constexpr uint8_t BOARD_ENCODER_B = 19;
static constexpr uint8_t BOARD_ENCODER_PUSH = 20;
static constexpr uint8_t BOARD_STATUS_LED = 18;
#else
#error "board_ble.h requires a ChainOSCPad BLE target."
#endif
