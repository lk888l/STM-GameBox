#include "app/app_module.hpp"

namespace gamebox::app {

bool AppModule::initialize()
{
    if (state_ == ModuleState::initialized) {
        return true;
    }
    if (state_ == ModuleState::cleanup_failed) {
        return false;
    }

    if (onInitialize()) {
        state_ = ModuleState::initialized;
        return true;
    }

    state_ = onDeinitialize() ? ModuleState::stopped : ModuleState::cleanup_failed;
    return false;
}

bool AppModule::deinitialize()
{
    if (state_ == ModuleState::stopped) {
        return true;
    }

    state_ = onDeinitialize() ? ModuleState::stopped : ModuleState::cleanup_failed;
    return state_ == ModuleState::stopped;
}

} // namespace gamebox::app
