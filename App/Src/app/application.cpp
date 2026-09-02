#include <new>

#include "app/application.hpp"

namespace gamebox::app {

Application::Application()
{
    configASSERT(input_.subscribe(diagnostics_));
    const RegistrationResult display_registration = manager_.registerModule(display_);
    const RegistrationResult input_registration = manager_.registerModule(input_);
    const RegistrationResult audio_registration = manager_.registerModule(audio_);
    const RegistrationResult diagnostics_registration = manager_.registerModule(diagnostics_);
    const RegistrationResult ui_registration = manager_.registerModule(ui_);
    configASSERT(static_cast<bool>(display_registration));
    configASSERT(static_cast<bool>(input_registration));
    configASSERT(static_cast<bool>(audio_registration));
    configASSERT(static_cast<bool>(diagnostics_registration));
    configASSERT(static_cast<bool>(ui_registration));
}

Application& Application::instance()
{
    // The firmware lifetime is the MCU uptime; it never runs C++ exit handlers.
    // Placement construction avoids registering a meaningless static destructor
    // while retaining lazy construction after CubeMX has initialized the HAL.
    alignas(Application) static unsigned char storage[sizeof(Application)]{};
    static Application* const application = new (storage) Application();
    return *application;
}

bool Application::initialize()
{
    if (manager_.isRunning()) {
        return true;
    }
    // The Cortex-M3 FreeRTOS port keeps maskable interrupts disabled after the
    // first kernel object is created and restores them when scheduling starts.
    // Complete every IRQ- or tick-dependent HAL operation before that point.
    const std::uint32_t boot_seed = entropy_.collectSeed();
    if (!display_.prepare()) {
        return false;
    }
    if (!ui_.prepare(boot_seed)) {
        return false;
    }
    return static_cast<bool>(manager_.initializeAll());
}

} // namespace gamebox::app
