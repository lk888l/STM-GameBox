#pragma once
#include "FreeRTOS.h"

BaseType_t xTaskGetSchedulerState(void);
void vTaskDelay(TickType_t ticks);
