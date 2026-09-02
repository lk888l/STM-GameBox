#pragma once

#include <cstddef>

#include "app/app_module.hpp"
#include "etl/array.h"
#include "etl/string_view.h"

namespace gamebox::app {

enum class RegistrationStatus : unsigned char {
    ok,
    null_module,
    duplicate_name,
    registry_full,
    invalid_state,
};

struct RegistrationResult {
    RegistrationStatus status{RegistrationStatus::ok};
    etl::string_view module_name{};

    [[nodiscard]] explicit operator bool() const { return status == RegistrationStatus::ok; }
};

enum class LifecycleStatus : unsigned char {
    ok,
    invalid_state,
    module_failed,
    rollback_failed,
};

struct LifecycleResult {
    LifecycleStatus status{LifecycleStatus::ok};
    etl::string_view module_name{};

    [[nodiscard]] explicit operator bool() const { return status == LifecycleStatus::ok; }
};

class AppManager {
public:
    static constexpr std::size_t kMaxModules = 8U;

    [[nodiscard]] RegistrationResult registerModule(AppModule& module);
    [[nodiscard]] LifecycleResult initializeAll();
    [[nodiscard]] LifecycleResult deinitializeAll();

    [[nodiscard]] bool isRunning() const { return state_ == State::running; }
    [[nodiscard]] bool hasCleanupFailure() const { return state_ == State::faulted; }
    [[nodiscard]] std::size_t moduleCount() const { return module_count_; }

private:
    enum class State : unsigned char {
        configuring,
        running,
        faulted,
    };

    etl::array<AppModule*, kMaxModules> modules_{};
    std::size_t module_count_{0U};
    State state_{State::configuring};
};

} // namespace gamebox::app
