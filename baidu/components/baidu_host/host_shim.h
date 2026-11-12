#pragma once
// Host stand-ins for the FreeRTOS and ESP-IDF types ESPHome's audio buffers use.
// A tick is one millisecond on host.
#include <cstdint>

using TickType_t = uint32_t;
using BaseType_t = int32_t;
using UBaseType_t = uint32_t;
#define pdMS_TO_TICKS(ms) ((TickType_t) (ms))
#define portMAX_DELAY ((TickType_t) 0xffffffffUL)
#define pdTRUE 1
#define pdFALSE 0

#include <cstdint>

using esp_err_t = int;
#define ESP_OK 0
#define ESP_FAIL -1
