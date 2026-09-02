#pragma once

#include "etl/string_view.h"

namespace gamebox::app {

enum class ModuleState : unsigned char {
    stopped,
    initialized,
    cleanup_failed,
};

class AppModule {
public:
    AppModule() = default;

    AppModule(const AppModule&) = delete;
    AppModule& operator=(const AppModule&) = delete;

    [[nodiscard]] bool initialize();
    [[nodiscard]] bool deinitialize();
    [[nodiscard]] bool isInitialized() const { return state_ == ModuleState::initialized; }
    [[nodiscard]] ModuleState state() const { return state_; }

    [[nodiscard]] virtual etl::string_view name() const = 0;

protected:
    // Modules are statically owned and never deleted through this base. Keeping
    // the non-virtual destructor protected enforces that ownership model and
    // avoids linking a heap-backed deleting-destructor.
    ~AppModule() = default;
    [[nodiscard]] virtual bool onInitialize() = 0;
    [[nodiscard]] virtual bool onDeinitialize() = 0;

private:
    ModuleState state_{ModuleState::stopped};
};

} // namespace gamebox::app
