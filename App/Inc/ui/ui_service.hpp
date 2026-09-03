#pragma once

#include <cstddef>
#include <cstdint>

#include "app/app_module.hpp"
#include "app/app_task.hpp"
#include "audio/audio_service.hpp"
#include "display/canvas.hpp"
#include "display/ssd1306.hpp"
#include "diagnostics/uart_dma_service.hpp"
#include "etl/string.h"
#include "games/air_raid_game.hpp"
#include "games/dino_game.hpp"
#include "games/game_types.hpp"
#include "games/pong_game.hpp"
#include "games/snake_game.hpp"
#include "games/tetris_game.hpp"
#include "input/input_service.hpp"
#include "platform/rtc_calendar.hpp"
#include "storage/settings_store.hpp"
#include "ui/menu_model.hpp"
#include "ui/tween.hpp"

namespace gamebox::ui {

class UiService final : public app::AppModule, private app::AppTask {
public:
    UiService(display::Ssd1306& display,
              input::InputService& input,
              audio::AudioService& audio,
              diagnostics::UartDmaService& diagnostics,
              storage::SettingsStore& settings,
              platform::RtcCalendar& calendar);

    [[nodiscard]] etl::string_view name() const override { return "ui"; }
    /** Performs all HAL operations before the first FreeRTOS object is created. */
    [[nodiscard]] bool prepare(std::uint32_t boot_seed);

protected:
    [[nodiscard]] bool onInitialize() override;
    [[nodiscard]] bool onDeinitialize() override;

private:
    enum class MotionLevel : std::uint8_t { full, reduced, off };

    static constexpr std::size_t kViewCount = static_cast<std::size_t>(View::count);
    static constexpr std::size_t kHistoryCapacity = 8U;

    void run() override;
    void handleEvent(const input::ButtonEvent& event, std::uint32_t now_ms);
    void handleMenuEvent(const input::ButtonEvent& event, std::uint32_t now_ms);
    void handleSnakeEvent(const input::ButtonEvent& event, std::uint32_t now_ms);
    void handleDinoEvent(const input::ButtonEvent& event, std::uint32_t now_ms);
    void handleAirRaidEvent(const input::ButtonEvent& event, std::uint32_t now_ms);
    void handleTetrisEvent(const input::ButtonEvent& event, std::uint32_t now_ms);
    void handlePongEvent(const input::ButtonEvent& event, std::uint32_t now_ms);
    void handlePianoEvent(const input::ButtonEvent& event, std::uint32_t now_ms);
    void handleStopwatchEvent(const input::ButtonEvent& event, std::uint32_t now_ms);
    void handleTimerEvent(const input::ButtonEvent& event, std::uint32_t now_ms);
    void handleClockEvent(const input::ButtonEvent& event, std::uint32_t now_ms);
    void adjustClockField(std::int8_t direction);
    void moveSelection(std::int8_t delta, std::uint32_t now_ms);
    void activateSelection(std::uint32_t now_ms);
    void openView(View target, std::uint32_t now_ms, bool remember = true);
    void navigateBack(std::uint32_t now_ms);
    void update(std::uint32_t now_ms);

    void render(std::uint32_t now_ms);
    void renderView(View view,
                    std::int16_t x_offset,
                    std::uint32_t now_ms,
                    bool interactive);
    void renderHome(std::int16_t x_offset, bool interactive);
    void renderHomeCard(const MenuEntry& entry,
                        std::uint8_t index,
                        std::int16_t x_offset);
    void renderList(View view,
                    std::int16_t x_offset,
                    bool interactive);
    void renderClock(std::int16_t x_offset, std::uint32_t now_ms);
    void renderStopwatch(std::int16_t x_offset, std::uint32_t now_ms);
    void renderTimer(std::int16_t x_offset, std::uint32_t now_ms);
    void renderInputLab(std::int16_t x_offset);
    void renderSystem(std::int16_t x_offset, std::uint32_t now_ms);
    void renderAbout(std::int16_t x_offset);
    void renderSnake(std::int16_t x_offset, std::uint32_t now_ms);
    void renderDino(std::int16_t x_offset, std::uint32_t now_ms);
    void renderAirRaid(std::int16_t x_offset);
    void renderTetris(std::int16_t x_offset);
    void renderPong(std::int16_t x_offset);
    void renderPiano(std::int16_t x_offset);
    void renderGameOverlay(std::int16_t x_offset,
                           games::GamePhase state,
                           etl::string_view ready_hint = "ENTER TO PLAY");
    void renderHeader(std::int16_t x_offset, etl::string_view title);
    void renderFooter(std::int16_t x_offset, etl::string_view hint);
    void renderIcon(Icon icon, std::int16_t x, std::int16_t y);
    void renderToast(std::uint32_t now_ms);

    void showToast(etl::string_view message, std::uint32_t now_ms, std::uint32_t duration_ms = 1200U);
    void feedbackPulse(std::uint32_t now_ms);
    void setSetting(Action action, std::uint32_t now_ms);
    void persistSettings();
    [[nodiscard]] const char* settingValue(Action action) const;
    void stepMotion();
    void syncListMotion(View view);
    [[nodiscard]] SpringSpeed springSpeed() const;
    [[nodiscard]] std::int16_t selectionTargetY(View view) const;
    [[nodiscard]] std::int16_t selectionTargetWidth(View view) const;
    [[nodiscard]] std::int16_t scrollTargetY(View view) const;
    [[nodiscard]] static std::size_t viewIndex(View view);
    [[nodiscard]] static std::uint32_t toMilliseconds(TickType_t ticks);

    display::Ssd1306& display_;
    input::InputService& input_;
    audio::AudioService& audio_;
    diagnostics::UartDmaService& diagnostics_;
    storage::SettingsStore& settings_;
    platform::RtcCalendar& calendar_;
    display::Canvas canvas_{};
    games::SnakeGame snake_{};
    games::DinoGame dino_{};
    games::AirRaidGame air_raid_{};
    games::TetrisGame tetris_{};
    games::PongGame pong_{};
    std::uint8_t piano_note_{0xFFU};

    View current_view_{View::home};
    View previous_view_{View::home};
    View history_[kHistoryCapacity]{};
    std::size_t history_size_{0U};
    std::uint8_t selections_[kViewCount]{};
    std::uint8_t scroll_top_[kViewCount]{};
    std::uint8_t carousel_previous_{0U};
    std::int8_t carousel_direction_{1};
    Spring page_spring_{0};
    Spring selection_spring_{14};
    Spring selection_width_spring_{52};
    Spring scroll_spring_{0};
    Spring carousel_spring_{0};
    bool page_forward_{true};

    MotionLevel motion_{MotionLevel::full};
    bool sound_enabled_{true};
    std::uint8_t brightness_level_{3U};

    bool stopwatch_running_{false};
    std::uint32_t stopwatch_started_ms_{0U};
    std::uint32_t stopwatch_accumulated_ms_{0U};

    bool countdown_running_{false};
    std::uint32_t countdown_seconds_{300U};
    std::uint32_t countdown_deadline_ms_{0U};

    std::uint32_t system_stack_free_bytes_{0U};
    std::uint32_t system_next_stack_sample_ms_{0U};

    platform::DateTime clock_snapshot_{};
    platform::DateTime clock_edit_{};
    std::uint32_t clock_next_refresh_ms_{0U};
    std::uint8_t clock_field_{0U};
    bool clock_snapshot_valid_{false};
    bool clock_editing_{false};

    input::ButtonEvent last_event_{};
    std::uint32_t event_count_{0U};
    std::uint32_t boot_seed_{0U};
    etl::string<24> toast_{};
    std::uint32_t toast_until_ms_{0U};
    bool prepared_{false};
};

} // namespace gamebox::ui
