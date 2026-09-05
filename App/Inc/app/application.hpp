#pragma once

#include "app/app_manager.hpp"
#include "audio/audio_service.hpp"
#include "display/ssd1306.hpp"
#include "diagnostics/uart_dma_service.hpp"
#include "input/input_service.hpp"
#include "platform/entropy_source.hpp"
#include "platform/rtc_calendar.hpp"
#include "storage/settings_store.hpp"
#include "ui/ui_service.hpp"

namespace gamebox::app {

class Application final {
public:
    static Application& instance();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    [[nodiscard]] bool initialize();

private:
    Application();

    AppManager manager_{};
#if GAMEBOX_OLED_SPI
    display::Ssd1306 display_{hspi1};
#else
    display::Ssd1306 display_{hi2c1};
#endif
    input::InputService input_{};
    audio::AudioService audio_{};
    diagnostics::UartDmaService diagnostics_{huart1};
    storage::SettingsStore settings_{hrtc};
    platform::RtcCalendar calendar_{hrtc};
    platform::EntropySource entropy_{hadc1};
    ui::UiService ui_{display_, input_, audio_, diagnostics_, settings_, calendar_};
};

} // namespace gamebox::app
