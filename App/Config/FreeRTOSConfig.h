#pragma once

#include <stddef.h>
#include <stdint.h>

#include "stm32f1xx.h"

/* Kernel fundamentals ----------------------------------------------------- */
#define configCPU_CLOCK_HZ                         ( SystemCoreClock )
#define configTICK_RATE_HZ                         1000U
#define configTICK_TYPE_WIDTH_IN_BITS              TICK_TYPE_WIDTH_32_BITS
#define configUSE_PREEMPTION                       1
#define configUSE_TIME_SLICING                     1
#define configNUMBER_OF_CORES                      1
#define configRUN_MULTIPLE_PRIORITIES              1
#define configMAX_PRIORITIES                       6U
#define configMINIMAL_STACK_SIZE                   128U
#define configMAX_TASK_NAME_LEN                    16U
#define configIDLE_SHOULD_YIELD                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION    1

/* Static-memory-only policy ---------------------------------------------- */
#define configSUPPORT_STATIC_ALLOCATION            1
#define configKERNEL_PROVIDED_STATIC_MEMORY        0
#define configSUPPORT_DYNAMIC_ALLOCATION           0
#define configAPPLICATION_ALLOCATED_HEAP           0
#define configENABLE_HEAP_PROTECTOR                0

/* Hooks and diagnostics -------------------------------------------------- */
#define configUSE_IDLE_HOOK                        1
#define configUSE_TICK_HOOK                        0
#define configUSE_MALLOC_FAILED_HOOK               0
#define configCHECK_FOR_STACK_OVERFLOW             2
#define configRECORD_STACK_HIGH_ADDRESS            1
#define configUSE_TRACE_FACILITY                   0
#define configGENERATE_RUN_TIME_STATS              0
#define configUSE_STATS_FORMATTING_FUNCTIONS       0
#define configQUEUE_REGISTRY_SIZE                  0U
#define configCHECK_HANDLER_INSTALLATION           1
#define configCONTROL_INFINITE_LOOP()              1
#define configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES  0

/* Optional kernel features ---------------------------------------------- */
#define configUSE_TASK_NOTIFICATIONS               1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES      1U
#define configUSE_MUTEXES                          0
#define configUSE_RECURSIVE_MUTEXES                0
#define configUSE_COUNTING_SEMAPHORES              0
#define configUSE_QUEUE_SETS                       0
#define configUSE_TIMERS                           0
#define configUSE_EVENT_GROUPS                     0
#define configUSE_STREAM_BUFFERS                   0
#define configUSE_CO_ROUTINES                      0
#define configUSE_DAEMON_TASK_STARTUP_HOOK         0
#define configUSE_APPLICATION_TASK_TAG             0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS    0
#define configUSE_NEWLIB_REENTRANT                 0
#define configUSE_POSIX_ERRNO                      0
#define configUSE_TICKLESS_IDLE                    0
#define configENABLE_BACKWARD_COMPATIBILITY        0

/* API surface ------------------------------------------------------------ */
#define INCLUDE_vTaskPrioritySet                   0
#define INCLUDE_uxTaskPriorityGet                  0
#define INCLUDE_vTaskDelete                       1
#define INCLUDE_vTaskSuspend                      0
#define INCLUDE_vTaskDelay                        1
#define INCLUDE_xTaskDelayUntil                   1
#define INCLUDE_xTaskAbortDelay                   0
#define INCLUDE_xTaskGetSchedulerState            1
#define INCLUDE_xTaskGetCurrentTaskHandle         1
#define INCLUDE_uxTaskGetStackHighWaterMark       1
#define INCLUDE_uxTaskGetStackHighWaterMark2      1
#define INCLUDE_eTaskGetState                     0
#define INCLUDE_xTaskGetIdleTaskHandle            0
#define INCLUDE_xTaskGetHandle                    0
#define INCLUDE_xTimerPendFunctionCall            0
#define INCLUDE_xTaskResumeFromISR                0

/* Cortex-M3 interrupt priorities.  Lower numeric values are more urgent. */
#define configPRIO_BITS                            __NVIC_PRIO_BITS
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY   15U
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5U
#define configKERNEL_INTERRUPT_PRIORITY \
    ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8U - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8U - configPRIO_BITS ) )

/* Route the Cortex exception vectors directly to the naked FreeRTOS port
 * handlers.  The CubeMX placeholder handlers are renamed for this one source
 * file by CMake so generated code remains untouched and regeneratable. */
#define vPortSVCHandler       SVC_Handler
#define xPortPendSVHandler    PendSV_Handler
#define xPortSysTickHandler   SysTick_Handler

#ifdef __cplusplus
extern "C" {
#endif
void vApplicationAssert(const char* file, int line);
#ifdef __cplusplus
}
#endif

#define configASSERT(condition)                                      \
    do                                                               \
    {                                                                \
        if (!(condition))                                            \
        {                                                            \
            vApplicationAssert(__FILE__, __LINE__);                  \
        }                                                            \
    } while (0)
