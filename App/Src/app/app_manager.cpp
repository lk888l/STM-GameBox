#include "app/app_manager.hpp"

namespace gamebox::app {

RegistrationResult AppManager::registerModule(AppModule& module)
{
    const etl::string_view module_name = module.name();
    if (state_ != State::configuring) {
        return {RegistrationStatus::invalid_state, module_name};
    }
    if (module_name.empty()) {
        return {RegistrationStatus::null_module, module_name};
    }
    if (module_count_ >= modules_.size()) {
        return {RegistrationStatus::registry_full, module_name};
    }

    for (std::size_t index = 0U; index < module_count_; ++index) {
        if (modules_[index] == &module || modules_[index]->name() == module_name) {
            return {RegistrationStatus::duplicate_name, module_name};
        }
    }

    modules_[module_count_++] = &module;
    return {RegistrationStatus::ok, module_name};
}

LifecycleResult AppManager::initializeAll()
{
    if (state_ == State::running) {
        return {};
    }
    if (state_ != State::configuring) {
        return {LifecycleStatus::invalid_state, {}};
    }

    std::size_t initialized = 0U;
    for (; initialized < module_count_; ++initialized) {
        AppModule& module = *modules_[initialized];
        if (!module.initialize()) {
            const etl::string_view failed_name = module.name();
            bool rollback_failed = module.state() == ModuleState::cleanup_failed;
            etl::string_view rollback_name = rollback_failed ? failed_name : etl::string_view{};

            while (initialized > 0U) {
                AppModule& rollback = *modules_[--initialized];
                if (!rollback.deinitialize()) {
                    if (!rollback_failed) {
                        rollback_name = rollback.name();
                    }
                    rollback_failed = true;
                }
            }
            if (rollback_failed) {
                state_ = State::faulted;
                return {LifecycleStatus::rollback_failed, rollback_name};
            }
            return {LifecycleStatus::module_failed, failed_name};
        }
    }

    state_ = State::running;
    return {};
}

LifecycleResult AppManager::deinitializeAll()
{
    if (state_ == State::configuring) {
        return {};
    }

    bool cleanup_failed = false;
    etl::string_view failed_name{};
    for (std::size_t remaining = module_count_; remaining > 0U; --remaining) {
        AppModule& module = *modules_[remaining - 1U];
        if (module.state() != ModuleState::stopped && !module.deinitialize()) {
            if (!cleanup_failed) {
                failed_name = module.name();
            }
            cleanup_failed = true;
        }
    }

    if (cleanup_failed) {
        state_ = State::faulted;
        return {LifecycleStatus::module_failed, failed_name};
    }
    state_ = State::configuring;
    return {};
}

} // namespace gamebox::app
