#pragma once

#include <stdint.h>

typedef int BaseType_t;
typedef uint32_t TickType_t;
typedef struct { BaseType_t token; } StaticSemaphore_t;
typedef StaticSemaphore_t* SemaphoreHandle_t;

enum { pdFALSE = 0, pdTRUE = 1, taskSCHEDULER_NOT_STARTED = 0, taskSCHEDULER_RUNNING = 2 };
#define pdMS_TO_TICKS(milliseconds) (milliseconds)
#define portYIELD_FROM_ISR(woken) fake_yield_from_isr(woken)

void fake_yield_from_isr(BaseType_t woken);
