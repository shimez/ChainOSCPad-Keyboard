#if defined(CHAINOSCPAD_TARGET_ESP32S3_USB)
#include "firmware_s3.inc"
#elif defined(CHAINOSCPAD_TARGET_ESP32S3_BLE) || \
      defined(CHAINOSCPAD_TARGET_ESP32C3_BLE) || \
      defined(CHAINOSCPAD_TARGET_ESP32C5_BLE) || \
      defined(CHAINOSCPAD_TARGET_ESP32C6_BLE)
#include "firmware_ble.inc"
#else
#error "Select a ChainOSCPad PlatformIO environment."
#endif
