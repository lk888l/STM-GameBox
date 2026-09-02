#include "app_bridge.h"

#include "app/application.hpp"
#include "main.h"

extern "C" {
#include "FreeRTOS.h"
#include "task.h"
}

namespace {

[[noreturn]] void failFast()
{
    vApplicationAssert(__FILE__, __LINE__);
    for (;;) {
    }
}

} // namespace

extern "C" APP_NORETURN void App_Bootstrap(void)
{
    if (!gamebox::app::Application::instance().initialize()) {
        failFast();
    }

    vTaskStartScheduler();
    failFast();
}
