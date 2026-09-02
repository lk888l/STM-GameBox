#pragma once

#include <atomic>
#include <cstdint>

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
}

namespace gamebox::app {

class AppTask {
public:
    AppTask(const char* name,
            UBaseType_t priority,
            StackType_t* stack,
            std::uint32_t stack_depth,
            StaticTask_t& task_control_block);

    AppTask(const AppTask&) = delete;
    AppTask& operator=(const AppTask&) = delete;

    [[nodiscard]] bool start();
    [[nodiscard]] bool stop(TickType_t timeout = pdMS_TO_TICKS(1500U));
    void requestStop();

    [[nodiscard]] bool isRunning() const { return running_.load(std::memory_order_acquire); }
    /** Historical minimum unused stack, in bytes; zero before task creation. */
    [[nodiscard]] std::uint32_t stackHeadroomBytes() const;

protected:
    // Tasks are embedded in statically owned services; polymorphic deletion is
    // intentionally unavailable.
    ~AppTask() = default;
    [[nodiscard]] bool shouldExit() const
    {
        return exit_requested_.load(std::memory_order_acquire);
    }

    /** Waits for a stop notification or for timeout ticks to expire. */
    void waitInterruptible(TickType_t timeout) const;

    virtual void run() = 0;
    virtual void cleanup() {}

private:
    static void entry(void* argument);

    const char* name_;
    UBaseType_t priority_;
    StackType_t* stack_;
    std::uint32_t stack_depth_;
    StaticTask_t& task_control_block_;
    std::atomic<TaskHandle_t> handle_{nullptr};
    std::atomic<bool> active_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> exit_requested_{false};
};

static_assert(std::atomic<TaskHandle_t>::is_always_lock_free,
              "Cortex-M3 task handles must be lock-free atomics");

} // namespace gamebox::app
