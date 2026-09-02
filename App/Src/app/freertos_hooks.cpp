#include <cstdint>

#include "main.h"

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
}

namespace {

StaticTask_t idle_task_control_block;
StackType_t idle_task_stack[configMINIMAL_STACK_SIZE];

[[noreturn]] void haltWithCode(const std::uint8_t code)
{
    std::uint16_t led_pins = 0U;
    led_pins |= ((code & 0x01U) != 0U) ? LED_1_Pin : 0U;
    led_pins |= ((code & 0x02U) != 0U) ? LED_2_Pin : 0U;
    led_pins |= ((code & 0x04U) != 0U) ? LED_3_Pin : 0U;
    led_pins |= ((code & 0x08U) != 0U) ? LED_4_Pin : 0U;

    taskDISABLE_INTERRUPTS();
    HAL_GPIO_WritePin(LED_1_GPIO_Port,
                      LED_1_Pin | LED_2_Pin | LED_3_Pin | LED_4_Pin,
                      GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_1_GPIO_Port, led_pins, GPIO_PIN_SET);
#if defined(DEBUG)
    __BKPT(0);
#endif
    for (;;) {
        __WFI();
    }
}

} // namespace

extern "C" void vApplicationGetIdleTaskMemory(StaticTask_t** task_control_block,
                                               StackType_t** stack,
                                               std::uint32_t* stack_size)
{
    *task_control_block = &idle_task_control_block;
    *stack = idle_task_stack;
    *stack_size = configMINIMAL_STACK_SIZE;
}

extern "C" void vApplicationIdleHook(void)
{
    __WFI();
}

extern "C" void vApplicationStackOverflowHook(TaskHandle_t task, char* task_name)
{
    (void)task;
    (void)task_name;
    haltWithCode(0x0EU);
}

extern "C" void vApplicationAssert(const char* file, const int line)
{
    (void)file;
    (void)line;
    haltWithCode(0x0FU);
}
