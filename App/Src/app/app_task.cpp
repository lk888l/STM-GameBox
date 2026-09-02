#include "app/app_task.hpp"

namespace gamebox::app {

AppTask::AppTask(const char* name,
                 const UBaseType_t priority,
                 StackType_t* const stack,
                 const std::uint32_t stack_depth,
                 StaticTask_t& task_control_block)
    : name_(name),
      priority_(priority),
      stack_(stack),
      stack_depth_(stack_depth),
      task_control_block_(task_control_block)
{
    configASSERT(name_ != nullptr);
    configASSERT(stack_ != nullptr);
    configASSERT(stack_depth_ >= configMINIMAL_STACK_SIZE);
}

bool AppTask::start()
{
    if (active_.exchange(true, std::memory_order_acq_rel)) {
        return false;
    }

    exit_requested_.store(false, std::memory_order_release);
    const TaskHandle_t existing = handle_.load(std::memory_order_acquire);
    if (existing != nullptr) {
        xTaskNotifyGive(existing);
        return true;
    }

    taskENTER_CRITICAL();
    TaskHandle_t handle = xTaskCreateStatic(entry,
                                            name_,
                                            stack_depth_,
                                            this,
                                            priority_,
                                            stack_,
                                            &task_control_block_);
    handle_.store(handle, std::memory_order_release);
    taskEXIT_CRITICAL();
    if (handle == nullptr) {
        active_.store(false, std::memory_order_release);
    }
    return handle != nullptr;
}

std::uint32_t AppTask::stackHeadroomBytes() const
{
    const TaskHandle_t handle = handle_.load(std::memory_order_acquire);
    if (handle == nullptr) {
        return 0U;
    }
    return static_cast<std::uint32_t>(uxTaskGetStackHighWaterMark(handle)) *
           static_cast<std::uint32_t>(sizeof(StackType_t));
}

void AppTask::requestStop()
{
    exit_requested_.store(true, std::memory_order_release);
    const TaskHandle_t handle = handle_.load(std::memory_order_acquire);
    if (handle != nullptr && xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xTaskNotifyGive(handle);
    }
}

bool AppTask::stop(const TickType_t timeout)
{
    TaskHandle_t handle = handle_.load(std::memory_order_acquire);
    if (handle == nullptr) {
        return true;
    }

    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        vTaskDelete(handle);
        handle_.store(nullptr, std::memory_order_release);
        active_.store(false, std::memory_order_release);
        running_.store(false, std::memory_order_release);
        return true;
    }

    if (handle == xTaskGetCurrentTaskHandle()) {
        return false;
    }

    if (!active_.load(std::memory_order_acquire)) {
        return true;
    }

    requestStop();
    const TickType_t started = xTaskGetTickCount();
    while (active_.load(std::memory_order_acquire)) {
        if (timeout != portMAX_DELAY && (xTaskGetTickCount() - started) >= timeout) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(2U));
    }
    return true;
}

void AppTask::waitInterruptible(const TickType_t timeout) const
{
    (void)ulTaskNotifyTake(pdTRUE, timeout);
}

void AppTask::entry(void* const argument)
{
    auto& self = *static_cast<AppTask*>(argument);
    for (;;) {
        while (!self.active_.load(std::memory_order_acquire)) {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
        // Remove a stale stop/wake notification before entering the service loop.
        (void)ulTaskNotifyTake(pdTRUE, 0U);
        self.running_.store(true, std::memory_order_release);
        self.run();
        self.cleanup();
        self.running_.store(false, std::memory_order_release);
        self.active_.store(false, std::memory_order_release);
    }
}

} // namespace gamebox::app
