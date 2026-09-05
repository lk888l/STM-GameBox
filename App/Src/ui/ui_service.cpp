#include "ui/ui_service.hpp"

namespace gamebox::ui {

namespace {

#if GAMEBOX_OLED_SPI
constexpr TickType_t kFramePeriod = pdMS_TO_TICKS(5U);
#else
constexpr TickType_t kFramePeriod = pdMS_TO_TICKS(33U);
#endif
constexpr std::uint32_t kUiStackDepth = 384U;
constexpr std::uint8_t kVisibleRows = 3U;
constexpr std::int16_t kFirstRowY = 14;
constexpr std::int16_t kRowHeight = 16;
constexpr std::int16_t kSelectionHeight = 15;
constexpr std::uint16_t kPetFrames[2][9] = {
    {0x0900U, 0x0F80U, 0x0BC2U, 0x0FFDU, 0x07FEU,
     0x07F8U, 0x01F8U, 0x0148U, 0x0244U},
    {0x0900U, 0x0F80U, 0x0BC2U, 0x0FFDU, 0x07FEU,
     0x07F8U, 0x01F8U, 0x0250U, 0x0290U},
};
static_assert(configTICK_RATE_HZ == 1000U,
              "GameBox timing assumes one FreeRTOS tick equals one millisecond");

StackType_t ui_stack[kUiStackDepth];
StaticTask_t ui_task_control_block;

void formatTwoDigits(const std::uint32_t value, char* const output)
{
    output[0] = static_cast<char>('0' + ((value / 10U) % 10U));
    output[1] = static_cast<char>('0' + (value % 10U));
}

void formatTime(const std::uint32_t total_seconds, char (&output)[9])
{
    const std::uint32_t hours = (total_seconds / 3600U) % 100U;
    const std::uint32_t minutes = (total_seconds / 60U) % 60U;
    const std::uint32_t seconds = total_seconds % 60U;
    formatTwoDigits(hours, &output[0]);
    output[2] = ':';
    formatTwoDigits(minutes, &output[3]);
    output[5] = ':';
    formatTwoDigits(seconds, &output[6]);
    output[8] = '\0';
}

void formatDate(const storage::CalendarDate& date, char (&output)[11])
{
    output[0] = '2';
    output[1] = '0';
    formatTwoDigits(date.year, &output[2]);
    output[4] = '-';
    formatTwoDigits(date.month, &output[5]);
    output[7] = '-';
    formatTwoDigits(date.day, &output[8]);
    output[10] = '\0';
}

etl::string_view unsignedText(std::uint32_t value, char (&buffer)[11])
{
    char* cursor = &buffer[10];
    *cursor = '\0';
    do {
        --cursor;
        *cursor = static_cast<char>('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);
    return {cursor, static_cast<std::size_t>(&buffer[10] - cursor)};
}

std::int16_t textWidth(const etl::string_view text)
{
    constexpr std::size_t maximum_characters = 20U;
    const std::size_t characters =
        text.size() < maximum_characters ? text.size() : maximum_characters;
    return static_cast<std::int16_t>(characters * 6U);
}

void formatPosition(const std::uint8_t index,
                    const std::uint8_t count,
                    char (&output)[6])
{
    const std::uint8_t position = static_cast<std::uint8_t>(index + 1U);
    output[0] = static_cast<char>('0' + position / 10U);
    output[1] = static_cast<char>('0' + position % 10U);
    output[2] = '/';
    output[3] = static_cast<char>('0' + count / 10U);
    output[4] = static_cast<char>('0' + count % 10U);
    output[5] = '\0';
}

const char* buttonName(const input::Button button)
{
    switch (button) {
    case input::Button::up: return "UP";
    case input::Button::down: return "DOWN";
    case input::Button::left: return "LEFT";
    case input::Button::right: return "RIGHT";
    case input::Button::jump: return "JUMP";
    case input::Button::function: return "FUNC";
    case input::Button::enter: return "ENTER";
    case input::Button::back: return "BACK";
    default: return "?";
    }
}

const char* eventName(const input::ButtonEventType type)
{
    switch (type) {
    case input::ButtonEventType::pressed: return "PRESSED";
    case input::ButtonEventType::released: return "RELEASED";
    case input::ButtonEventType::click: return "CLICK";
    case input::ButtonEventType::double_click: return "DOUBLE";
    case input::ButtonEventType::long_press: return "LONG";
    case input::ButtonEventType::repeat: return "REPEAT";
    default: return "?";
    }
}

} // namespace

UiService::UiService(display::Ssd1306& display,
                     input::InputService& input,
                     audio::AudioService& audio,
                     diagnostics::UartDmaService& diagnostics,
                     storage::SettingsStore& settings,
                     platform::RtcCalendar& calendar)
    : AppTask("ui", 2U, ui_stack, kUiStackDepth, ui_task_control_block),
      display_(display),
      input_(input),
      audio_(audio),
      diagnostics_(diagnostics),
      settings_(settings),
      calendar_(calendar)
{
}

bool UiService::prepare(const std::uint32_t boot_seed)
{
    if (!calendar_.restore() || !calendar_.read(clock_snapshot_)) {
        return false;
    }
    clock_snapshot_valid_ = true;
    clock_next_refresh_ms_ = 0U;
    clock_editing_ = false;
    storage::SettingsData stored{};
    if (settings_.load(stored)) {
        sound_enabled_ = stored.sound_enabled;
        motion_ = static_cast<MotionLevel>(stored.motion_level);
        brightness_level_ = stored.brightness_level;
        home_header_mode_ = stored.home_header_mode;
    } else {
        persistSettings();
    }
    audio_.setEnabled(sound_enabled_);
    constexpr std::uint8_t contrast[] = {0x28U, 0x60U, 0xA0U, 0xCFU};
    // The display caches this setting and retries an unavailable link in flush.
    (void)display_.setContrast(contrast[brightness_level_]);
    current_view_ = View::home;
    previous_view_ = current_view_;
    history_size_ = 0U;
    processed_events_.store(0U, std::memory_order_relaxed);
    maximum_press_age_ms_.store(0U, std::memory_order_relaxed);
    rendered_frames_.store(0U, std::memory_order_relaxed);
    maximum_render_time_ms_.store(0U, std::memory_order_relaxed);
    confirmation_guard_.reset();
    page_spring_.snapTo(0);
    selection_spring_.snapTo(kFirstRowY);
    selection_width_spring_.snapTo(52);
    scroll_spring_.snapTo(0);
    carousel_spring_.snapTo(0);
    boot_seed_ = boot_seed;
    snake_.reset(boot_seed_);
    canvas_.clear();
    canvas_.forceDirty();
    (void)display_.flush(canvas_);
    prepared_ = true;
    return true;
}

bool UiService::onInitialize()
{
    return prepared_ && start();
}

bool UiService::onDeinitialize()
{
    audio_.setEnabled(false);
    if (!stop()) {
        return false;
    }
    prepared_ = false;
    return true;
}

std::size_t UiService::viewIndex(const View view)
{
    return static_cast<std::size_t>(view);
}

std::uint32_t UiService::toMilliseconds(const TickType_t ticks)
{
    return static_cast<std::uint32_t>(ticks);
}

SpringSpeed UiService::springSpeed() const
{
    switch (motion_) {
    case MotionLevel::full: return SpringSpeed::fast;
    case MotionLevel::reduced: return SpringSpeed::slow;
    case MotionLevel::off: return SpringSpeed::off;
    }
    return SpringSpeed::off;
}

void UiService::stepMotion(const std::uint32_t now_ms)
{
    const SpringSpeed speed = springSpeed();
    const std::uint32_t steps = motion_clock_.advance(now_ms);
    for (std::uint32_t step = 0U; step < steps; ++step) {
        page_spring_.step(speed);
        selection_spring_.step(speed);
        selection_width_spring_.step(speed);
        scroll_spring_.step(speed);
        carousel_spring_.step(speed);
    }
}

void UiService::run()
{
    motion_clock_.reset(toMilliseconds(xTaskGetTickCount()));
    while (!shouldExit()) {
        const TickType_t frame_started = xTaskGetTickCount();
        input::ButtonEvent event{};
        // Bound queue draining as well as rendering: arrivals during handling
        // must not defer the frame or its mandatory scheduling point forever.
        for (std::uint8_t handled = 0U; handled < 32U && input_.receive(event); ++handled) {
            handleEvent(event, toMilliseconds(xTaskGetTickCount()));
        }
        const std::uint32_t now_ms = toMilliseconds(xTaskGetTickCount());
        update(now_ms);
        const TickType_t render_started = xTaskGetTickCount();
        render(now_ms);
        const auto render_ms = toMilliseconds(xTaskGetTickCount() - render_started);
        if (render_ms > maximum_render_time_ms_.load(std::memory_order_relaxed)) {
            maximum_render_time_ms_.store(render_ms, std::memory_order_relaxed);
        }
        (void)rendered_frames_.fetch_add(1U, std::memory_order_relaxed);
        (void)display_.flush(canvas_);

        // Drop overdue frame ticks after slow transfers. A real block also
        // lets lower-priority services run when an unchanged scene skips DMA.
        TickType_t next_wake = frame_started;
        if (xTaskPeriodicDelay(&next_wake, kFramePeriod) == pdFALSE) {
            vTaskDelay(1U);
        }
    }
}

void UiService::feedbackPulse(const std::uint32_t now_ms)
{
    (void)now_ms;
    if (!sound_enabled_) {
        return;
    }
    (void)audio_.play({1350U, 12U});
}

void UiService::update(const std::uint32_t now_ms)
{
    stepMotion(now_ms);

    if (current_view_ == View::snake) {
        snake_.update(now_ms);
    } else if (current_view_ == View::dino) {
        dino_.update(now_ms);
    } else if (current_view_ == View::air_raid) {
        const input::ButtonMask pressed = input_.pressedMask();
        const bool up = (pressed & input::maskFor(input::Button::up)) != 0U;
        const bool down = (pressed & input::maskFor(input::Button::down)) != 0U;
        air_raid_.setVertical(up == down ? 0 : (up ? -1 : 1));
        air_raid_.update(now_ms);
    } else if (current_view_ == View::pong) {
        const input::ButtonMask pressed = input_.pressedMask();
        const bool left_up = (pressed & input::maskFor(input::Button::up)) != 0U;
        const bool left_down = (pressed & input::maskFor(input::Button::down)) != 0U;
        const bool right_up = (pressed & input::maskFor(input::Button::jump)) != 0U;
        const bool right_down = (pressed & input::maskFor(input::Button::function)) != 0U;
        pong_.setLeftDirection(left_up == left_down ? 0 : (left_up ? -1 : 1));
        pong_.setRightDirection(right_up == right_down ? 0 : (right_up ? -1 : 1));
        pong_.update(now_ms);
    } else if (current_view_ == View::tetris) {
        tetris_.update(now_ms);
    }

    if (countdown_running_ &&
        static_cast<std::int32_t>(now_ms - countdown_deadline_ms_) >= 0) {
        countdown_running_ = false;
        countdown_seconds_ = 0U;
        showToast("TIME UP", now_ms, 2500U);
        if (sound_enabled_) {
            (void)audio_.play({880U, 600U});
        }
    }
}

void UiService::handleEvent(const input::ButtonEvent& event, const std::uint32_t now_ms)
{
    last_event_ = event;
    ++event_count_;
    (void)processed_events_.fetch_add(1U, std::memory_order_relaxed);
    if (event.type == input::ButtonEventType::pressed) {
        const std::uint32_t age_ms = now_ms - event.timestamp_ms;
        if (age_ms > maximum_press_age_ms_.load(std::memory_order_relaxed)) {
            maximum_press_age_ms_.store(age_ms, std::memory_order_relaxed);
        }
    }

    // Motion is presentation state, not an input lock. Retargeting a transition
    // is preferable to silently discarding a valid button event.

    if (confirmation_guard_.consume(event)) {
        return;
    }

    // Piano deliberately uses Back as its eighth note. A long Back press exits.
    if (current_view_ == View::piano) {
        handlePianoEvent(event, now_ms);
        return;
    }

    if (current_view_ == View::clock && clock_editing_) {
        if (event.button == input::Button::back &&
            event.type == input::ButtonEventType::pressed) {
            clock_editing_ = false;
            showToast("CANCELLED", now_ms);
            feedbackPulse(now_ms);
        } else {
            handleClockEvent(event, now_ms);
        }
        return;
    }

    if (!isGame(current_view_) && event.button == input::Button::function &&
        event.type == input::ButtonEventType::long_press) {
        openView(View::input_lab, now_ms);
        return;
    }
    if (!isGame(current_view_) && event.button == input::Button::function &&
        event.type == input::ButtonEventType::double_click) {
        openView(View::clock, now_ms);
        return;
    }
    if (event.button == input::Button::back &&
        event.type == input::ButtonEventType::pressed) {
        navigateBack(now_ms);
        return;
    }

    switch (current_view_) {
    case View::home:
    case View::games:
    case View::tools:
    case View::settings:
        handleMenuEvent(event, now_ms);
        break;
    case View::snake:
        handleSnakeEvent(event, now_ms);
        break;
    case View::dino:
        handleDinoEvent(event, now_ms);
        break;
    case View::air_raid:
        handleAirRaidEvent(event, now_ms);
        break;
    case View::tetris:
        handleTetrisEvent(event, now_ms);
        break;
    case View::pong:
        handlePongEvent(event, now_ms);
        break;
    case View::stopwatch:
        handleStopwatchEvent(event, now_ms);
        break;
    case View::timer:
        handleTimerEvent(event, now_ms);
        break;
    case View::clock:
        handleClockEvent(event, now_ms);
        break;
    default:
        break;
    }
}

void UiService::handleMenuEvent(const input::ButtonEvent& event,
                                const std::uint32_t now_ms)
{
    const bool navigation_event = event.type == input::ButtonEventType::pressed ||
                                  event.type == input::ButtonEventType::repeat;
    if (navigation_event) {
        if (current_view_ == View::home) {
            if (event.button == input::Button::left || event.button == input::Button::up) {
                moveSelection(-1, now_ms);
            } else if (event.button == input::Button::right ||
                       event.button == input::Button::down) {
                moveSelection(1, now_ms);
            }
        } else if (event.button == input::Button::up) {
            moveSelection(-1, now_ms);
        } else if (event.button == input::Button::down) {
            moveSelection(1, now_ms);
        } else if (current_view_ == View::settings &&
                   (event.button == input::Button::left ||
                    event.button == input::Button::right)) {
            const MenuEntry& entry = entryAt(current_view_, selections_[viewIndex(current_view_)]);
            setSetting(entry.action, now_ms);
        }
    }

    if (isImmediateConfirmation(event)) {
        // Menu confirmation is edge-triggered. Click is deliberately delayed
        // by the 280 ms double-click window, so it is unsuitable for primary
        // navigation. Springs remain presentation state and can be retargeted.
        if (current_view_ == View::home) {
            carousel_spring_.snapTo(0);
        }
        confirmation_guard_.begin(event.button);
        activateSelection(now_ms);
    } else if (event.button == input::Button::function &&
               event.type == input::ButtonEventType::click) {
        showToast(entryAt(current_view_, selections_[viewIndex(current_view_)]).subtitle,
                  now_ms,
                  1800U);
    }
}

void UiService::moveSelection(const std::int8_t delta, const std::uint32_t now_ms)
{
    const MenuDefinition* const menu = menuFor(current_view_);
    if (menu == nullptr || menu->count == 0U) {
        return;
    }
    const std::size_t index = viewIndex(current_view_);
    const std::uint8_t old_selection = selections_[index];
    const int next = static_cast<int>(old_selection) + delta;
    if (next < 0) {
        selections_[index] = static_cast<std::uint8_t>(menu->count - 1U);
    } else if (next >= menu->count) {
        selections_[index] = 0U;
    } else {
        selections_[index] = static_cast<std::uint8_t>(next);
    }

    if (current_view_ == View::home) {
        carousel_previous_ = old_selection;
        carousel_direction_ = delta >= 0 ? 1 : -1;
        carousel_spring_.snapTo(static_cast<std::int16_t>(
            carousel_direction_ * display::Canvas::kWidth));
        carousel_spring_.setTarget(0);
    } else {
        syncListMotion(current_view_);
    }
    feedbackPulse(now_ms);
}

void UiService::activateSelection(const std::uint32_t now_ms)
{
    const MenuEntry& entry = entryAt(current_view_, selections_[viewIndex(current_view_)]);
    switch (entry.action) {
    case Action::open:
        openView(entry.target, now_ms);
        break;
    case Action::toggle_sound:
    case Action::cycle_motion:
    case Action::cycle_brightness:
    case Action::cycle_home_header:
        setSetting(entry.action, now_ms);
        break;
    case Action::unavailable:
        showToast("NOT AVAILABLE", now_ms, 1800U);
        break;
    }
    feedbackPulse(now_ms);
}

void UiService::setSetting(const Action action, const std::uint32_t now_ms)
{
    switch (action) {
    case Action::toggle_sound:
        sound_enabled_ = !sound_enabled_;
        audio_.setEnabled(sound_enabled_);
        showToast(sound_enabled_ ? "SOUND ON" : "SOUND OFF", now_ms);
        break;
    case Action::cycle_motion:
        if (motion_ == MotionLevel::full) {
            motion_ = MotionLevel::reduced;
        } else if (motion_ == MotionLevel::reduced) {
            motion_ = MotionLevel::off;
        } else {
            motion_ = MotionLevel::full;
        }
        showToast(settingValue(action), now_ms);
        break;
    case Action::cycle_brightness: {
        const std::uint8_t next = static_cast<std::uint8_t>((brightness_level_ + 1U) % 4U);
        constexpr std::uint8_t contrast[] = {0x28U, 0x60U, 0xA0U, 0xCFU};
        brightness_level_ = next;
        if (display_.setContrast(contrast[next])) {
            showToast(settingValue(action), now_ms);
        } else {
            // OledTransport retries with this cached contrast. Persist the
            // matching UI setting even if the screen is currently offline.
            showToast("DISPLAY ERROR", now_ms, 1800U);
        }
        break;
    }
    case Action::cycle_home_header: {
        const auto next = static_cast<std::uint8_t>(home_header_mode_) + 1U;
        home_header_mode_ = static_cast<storage::HomeHeaderMode>(next % 4U);
        showToast(settingValue(action), now_ms);
        break;
    }
    default:
        break;
    }
    persistSettings();
}

void UiService::persistSettings()
{
    const storage::SettingsData data{
        sound_enabled_,
        static_cast<std::uint8_t>(motion_),
        brightness_level_,
        home_header_mode_,
    };
    (void)settings_.save(data);
}

const char* UiService::settingValue(const Action action) const
{
    switch (action) {
    case Action::toggle_sound: return sound_enabled_ ? "ON" : "OFF";
    case Action::cycle_motion:
        if (motion_ == MotionLevel::full) { return "FULL"; }
        if (motion_ == MotionLevel::reduced) { return "REDUCED"; }
        return "OFF";
    case Action::cycle_brightness: {
        constexpr const char* values[] = {"LOW", "MED", "HIGH", "MAX"};
        return values[brightness_level_];
    }
    case Action::cycle_home_header:
        switch (home_header_mode_) {
        case storage::HomeHeaderMode::time: return "TIME";
        case storage::HomeHeaderMode::date: return "DATE";
        case storage::HomeHeaderMode::pet: return "PET";
        case storage::HomeHeaderMode::title: return "TITLE";
        }
        return "TIME";
    default: return "";
    }
}

void UiService::openView(const View target,
                         const std::uint32_t now_ms,
                         const bool remember)
{
    if (target == current_view_) {
        return;
    }
    if (remember && history_size_ == kHistoryCapacity) {
        for (std::size_t index = 1U; index < kHistoryCapacity; ++index) {
            history_[index - 1U] = history_[index];
        }
        --history_size_;
    }
    if (remember) {
        history_[history_size_++] = current_view_;
    }
    previous_view_ = current_view_;
    current_view_ = target;
    page_forward_ = true;
    page_spring_.snapTo(display::Canvas::kWidth);
    page_spring_.setTarget(0);
    syncListMotion(current_view_);
    const std::uint32_t seed = boot_seed_ ^ now_ms ^ (event_count_ << 9U) ^ 0x9E3779B9U;
    switch (target) {
    case View::snake: snake_.reset(seed ^ 0x51A2E11FU); break;
    case View::dino: dino_.reset(seed ^ 0xD1702026U); break;
    case View::air_raid: air_raid_.reset(seed ^ 0xA17A1D55U); break;
    case View::tetris: tetris_.reset(seed ^ 0x7E715123U); break;
    case View::pong: pong_.reset(seed ^ 0xB0112026U); break;
    case View::piano: piano_note_ = 0xFFU; break;
    default: break;
    }
}

void UiService::navigateBack(const std::uint32_t now_ms)
{
    if (current_view_ == View::home) {
        return;
    }
    const View target = history_size_ > 0U ? history_[--history_size_] : View::home;
    previous_view_ = current_view_;
    current_view_ = target;
    page_forward_ = false;
    page_spring_.snapTo(static_cast<std::int16_t>(-display::Canvas::kWidth));
    page_spring_.setTarget(0);
    syncListMotion(current_view_);
    feedbackPulse(now_ms);
}

void UiService::handleSnakeEvent(const input::ButtonEvent& event,
                                 const std::uint32_t now_ms)
{
    const bool navigation = event.type == input::ButtonEventType::pressed ||
                            event.type == input::ButtonEventType::repeat;
    if (navigation) {
        if (event.button == input::Button::up) {
            snake_.turn(games::SnakeGame::Direction::up);
        } else if (event.button == input::Button::down) {
            snake_.turn(games::SnakeGame::Direction::down);
        } else if (event.button == input::Button::left) {
            snake_.turn(games::SnakeGame::Direction::left);
        } else if (event.button == input::Button::right) {
            snake_.turn(games::SnakeGame::Direction::right);
        }
    }
    if ((event.button == input::Button::enter || event.button == input::Button::jump) &&
        event.type == input::ButtonEventType::pressed) {
        if (snake_.state() == games::SnakeGame::State::game_over) {
            snake_.reset(now_ms ^ event_count_);
        }
        snake_.start();
        feedbackPulse(now_ms);
    }
}

void UiService::handleDinoEvent(const input::ButtonEvent& event,
                                const std::uint32_t now_ms)
{
    if (event.type != input::ButtonEventType::pressed ||
        (event.button != input::Button::jump && event.button != input::Button::up &&
         event.button != input::Button::enter)) {
        return;
    }
    if (dino_.state() == games::DinoGame::State::game_over) {
        dino_.reset(now_ms ^ event_count_ ^ 0xD170U);
    }
    dino_.start();
    dino_.jump();
    feedbackPulse(now_ms);
}

void UiService::handleAirRaidEvent(const input::ButtonEvent& event,
                                   const std::uint32_t now_ms)
{
    if (event.type != input::ButtonEventType::pressed) {
        return;
    }
    if (event.button == input::Button::enter) {
        if (air_raid_.state() == games::AirRaidGame::State::game_over) {
            air_raid_.reset(now_ms ^ event_count_ ^ 0xA17A1DU);
        }
        air_raid_.start();
        feedbackPulse(now_ms);
    } else if (event.button == input::Button::jump && air_raid_.fire()) {
        if (sound_enabled_) {
            (void)audio_.play({1700U, 18U});
        }
    }
}

void UiService::handleTetrisEvent(const input::ButtonEvent& event,
                                  const std::uint32_t now_ms)
{
    if (event.type == input::ButtonEventType::pressed &&
        event.button == input::Button::enter) {
        if (tetris_.state() == games::TetrisGame::State::game_over) {
            tetris_.reset(now_ms ^ event_count_ ^ 0x7E715U);
        }
        tetris_.start();
        feedbackPulse(now_ms);
        return;
    }
    if (tetris_.state() != games::TetrisGame::State::playing) {
        return;
    }
    const bool navigation = event.type == input::ButtonEventType::pressed ||
                            event.type == input::ButtonEventType::repeat;
    if (!navigation) {
        return;
    }
    if (event.button == input::Button::left) {
        (void)tetris_.move(-1);
    } else if (event.button == input::Button::right) {
        (void)tetris_.move(1);
    } else if (event.button == input::Button::down) {
        (void)tetris_.softDrop();
    } else if (event.button == input::Button::up &&
               event.type == input::ButtonEventType::pressed) {
        tetris_.hardDrop();
    } else if (event.button == input::Button::jump &&
               event.type == input::ButtonEventType::pressed) {
        (void)tetris_.rotate(-1);
    } else if (event.button == input::Button::function &&
               event.type == input::ButtonEventType::pressed) {
        (void)tetris_.rotate(1);
    }
}

void UiService::handlePongEvent(const input::ButtonEvent& event,
                                const std::uint32_t now_ms)
{
    if (event.type != input::ButtonEventType::pressed ||
        event.button != input::Button::enter) {
        return;
    }
    if (pong_.state() == games::PongGame::State::game_over) {
        pong_.reset(now_ms ^ event_count_ ^ 0xB011U);
    }
    pong_.start();
    feedbackPulse(now_ms);
}

void UiService::handlePianoEvent(const input::ButtonEvent& event,
                                 const std::uint32_t now_ms)
{
    if (event.button == input::Button::back &&
        event.type == input::ButtonEventType::long_press) {
        navigateBack(now_ms);
        return;
    }
    const bool trigger = event.button == input::Button::back
                             ? event.type == input::ButtonEventType::click
                             : event.type == input::ButtonEventType::pressed;
    if (!trigger) {
        return;
    }

    std::uint8_t note = 0U;
    switch (event.button) {
    case input::Button::up: note = 0U; break;
    case input::Button::left: note = 1U; break;
    case input::Button::right: note = 2U; break;
    case input::Button::down: note = 3U; break;
    case input::Button::jump: note = 4U; break;
    case input::Button::function: note = 5U; break;
    case input::Button::enter: note = 6U; break;
    case input::Button::back: note = 7U; break;
    default: return;
    }
    constexpr std::uint16_t frequencies[] = {
        262U, 294U, 330U, 349U, 392U, 440U, 494U, 523U,
    };
    piano_note_ = note;
    if (sound_enabled_) {
        (void)audio_.play({frequencies[note], 180U});
    }
}

void UiService::handleStopwatchEvent(const input::ButtonEvent& event,
                                     const std::uint32_t now_ms)
{
    if (isImmediateConfirmation(event)) {
        if (stopwatch_running_) {
            stopwatch_accumulated_ms_ += now_ms - stopwatch_started_ms_;
        } else {
            stopwatch_started_ms_ = now_ms;
        }
        stopwatch_running_ = !stopwatch_running_;
        feedbackPulse(now_ms);
    } else if (event.button == input::Button::function &&
               event.type == input::ButtonEventType::click) {
        stopwatch_running_ = false;
        stopwatch_accumulated_ms_ = 0U;
        showToast("RESET", now_ms);
    }
}

void UiService::handleTimerEvent(const input::ButtonEvent& event,
                                 const std::uint32_t now_ms)
{
    const bool adjust = !countdown_running_ &&
                        (event.type == input::ButtonEventType::pressed ||
                         event.type == input::ButtonEventType::repeat);
    if (adjust) {
        if (event.button == input::Button::up) {
            countdown_seconds_ = countdown_seconds_ <= 3540U ? countdown_seconds_ + 60U : 3600U;
        } else if (event.button == input::Button::down) {
            countdown_seconds_ = countdown_seconds_ >= 60U ? countdown_seconds_ - 60U : 0U;
        } else if (event.button == input::Button::right) {
            countdown_seconds_ = countdown_seconds_ < 3600U ? countdown_seconds_ + 1U : 3600U;
        } else if (event.button == input::Button::left) {
            countdown_seconds_ = countdown_seconds_ > 0U ? countdown_seconds_ - 1U : 0U;
        }
    }
    if (isImmediateConfirmation(event)) {
        if (countdown_running_) {
            const std::int32_t remaining_ms =
                static_cast<std::int32_t>(countdown_deadline_ms_ - now_ms);
            countdown_seconds_ = remaining_ms > 0
                                     ? static_cast<std::uint32_t>(remaining_ms + 999) / 1000U
                                     : 0U;
            countdown_running_ = false;
        } else if (countdown_seconds_ > 0U) {
            countdown_deadline_ms_ = now_ms + countdown_seconds_ * 1000U;
            countdown_running_ = true;
        }
        feedbackPulse(now_ms);
    } else if (event.type == input::ButtonEventType::click &&
               event.button == input::Button::function) {
        countdown_running_ = false;
        countdown_seconds_ = 300U;
        showToast("SET 05:00", now_ms);
    }
}

void UiService::handleClockEvent(const input::ButtonEvent& event,
                                 const std::uint32_t now_ms)
{
    if (!clock_editing_) {
        if (isImmediateConfirmation(event)) {
            if (!calendar_.read(clock_edit_)) {
                showToast("RTC ERROR", now_ms, 1800U);
                return;
            }
            clock_edit_.seconds = 0U;
            clock_field_ = 0U;
            clock_editing_ = true;
            feedbackPulse(now_ms);
        }
        return;
    }

    const bool navigation = event.type == input::ButtonEventType::pressed ||
                            event.type == input::ButtonEventType::repeat;
    if (navigation) {
        if (event.button == input::Button::left) {
            clock_field_ = clock_field_ == 0U ? 4U
                                              : static_cast<std::uint8_t>(clock_field_ - 1U);
            feedbackPulse(now_ms);
        } else if (event.button == input::Button::right) {
            clock_field_ = static_cast<std::uint8_t>((clock_field_ + 1U) % 5U);
            feedbackPulse(now_ms);
        } else if (event.button == input::Button::up) {
            adjustClockField(1);
            feedbackPulse(now_ms);
        } else if (event.button == input::Button::down) {
            adjustClockField(-1);
            feedbackPulse(now_ms);
        }
    }

    if (isImmediateConfirmation(event)) {
        if (clock_field_ < 4U) {
            ++clock_field_;
        } else if (calendar_.set(clock_edit_)) {
            clock_snapshot_ = clock_edit_;
            clock_snapshot_valid_ = true;
            clock_next_refresh_ms_ = now_ms + 250U;
            clock_editing_ = false;
            showToast("CLOCK SAVED", now_ms);
        } else {
            showToast("RTC ERROR", now_ms, 1800U);
        }
        feedbackPulse(now_ms);
    } else if (event.button == input::Button::function &&
               event.type == input::ButtonEventType::click) {
        clock_editing_ = false;
        showToast("CANCELLED", now_ms);
        feedbackPulse(now_ms);
    }
}

void UiService::adjustClockField(const std::int8_t direction)
{
    const auto wrap = [direction](const std::uint8_t value,
                                  const std::uint8_t minimum,
                                  const std::uint8_t maximum) {
        if (direction > 0) {
            return value >= maximum ? minimum : static_cast<std::uint8_t>(value + 1U);
        }
        return value <= minimum ? maximum : static_cast<std::uint8_t>(value - 1U);
    };

    switch (clock_field_) {
    case 0U:
        clock_edit_.hours = wrap(clock_edit_.hours, 0U, 23U);
        break;
    case 1U:
        clock_edit_.minutes = wrap(clock_edit_.minutes, 0U, 59U);
        break;
    case 2U:
        clock_edit_.date.year = wrap(clock_edit_.date.year, 0U, 99U);
        break;
    case 3U:
        clock_edit_.date.month = wrap(clock_edit_.date.month, 1U, 12U);
        break;
    case 4U: {
        const std::uint8_t maximum =
            storage::CalendarCodec::daysInMonth(clock_edit_.date.year,
                                                clock_edit_.date.month);
        clock_edit_.date.day = wrap(clock_edit_.date.day, 1U, maximum);
        break;
    }
    default:
        break;
    }

    const std::uint8_t maximum =
        storage::CalendarCodec::daysInMonth(clock_edit_.date.year,
                                            clock_edit_.date.month);
    if (clock_edit_.date.day > maximum) {
        clock_edit_.date.day = maximum;
    }
}

std::int16_t UiService::selectionTargetY(const View view) const
{
    if (!isMenu(view) || view == View::home) {
        return kFirstRowY;
    }
    const std::size_t index = viewIndex(view);
    return static_cast<std::int16_t>(
        kFirstRowY + static_cast<std::int16_t>(selections_[index] - scroll_top_[index]) *
                            kRowHeight);
}

std::int16_t UiService::selectionTargetWidth(const View view) const
{
    if (!isMenu(view) || view == View::home) {
        return 52;
    }
    const MenuEntry& entry = entryAt(view, selections_[viewIndex(view)]);
    const std::int16_t width = static_cast<std::int16_t>(textWidth(entry.label) + 20);
    return width < 12 ? 12 : (width > 121 ? 121 : width);
}

std::int16_t UiService::scrollTargetY(const View view) const
{
    if (!isMenu(view) || view == View::home) {
        return 0;
    }
    return static_cast<std::int16_t>(
        -static_cast<std::int16_t>(scroll_top_[viewIndex(view)]) * kRowHeight);
}

void UiService::syncListMotion(const View view)
{
    const MenuDefinition* const menu = menuFor(view);
    if (menu == nullptr || view == View::home || menu->count == 0U) {
        return;
    }

    const std::size_t state_index = viewIndex(view);
    const std::uint8_t selected = selections_[state_index];
    const std::uint8_t maximum_top =
        menu->count > kVisibleRows ? static_cast<std::uint8_t>(menu->count - kVisibleRows) : 0U;
    const std::uint8_t centered_top = selected > 0U ? static_cast<std::uint8_t>(selected - 1U) : 0U;
    scroll_top_[state_index] = centered_top < maximum_top ? centered_top : maximum_top;
    selection_spring_.setTarget(selectionTargetY(view));
    selection_width_spring_.setTarget(selectionTargetWidth(view));
    scroll_spring_.setTarget(scrollTargetY(view));
}

void UiService::showToast(const etl::string_view message,
                          const std::uint32_t now_ms,
                          const std::uint32_t duration_ms)
{
    toast_.clear();
    for (const char character : message) {
        if (toast_.full()) {
            break;
        }
        toast_.push_back(character);
    }
    toast_until_ms_ = now_ms + duration_ms;
}

void UiService::render(const std::uint32_t now_ms)
{
    canvas_.clear();
    if (!page_spring_.settled()) {
        const std::int16_t current_x = page_spring_.value();
        const std::int16_t previous_x = static_cast<std::int16_t>(
            current_x + (page_forward_ ? -display::Canvas::kWidth : display::Canvas::kWidth));
        renderView(previous_view_, previous_x, now_ms, false);
        renderView(current_view_, current_x, now_ms, true);
    } else {
        renderView(current_view_, 0, now_ms, true);
    }
    renderToast(now_ms);
}

void UiService::renderView(const View view,
                           const std::int16_t x_offset,
                           const std::uint32_t now_ms,
                           const bool interactive)
{
    switch (view) {
    case View::home: renderHome(x_offset, now_ms, interactive); break;
    case View::games:
    case View::tools:
    case View::settings: renderList(view, x_offset, interactive); break;
    case View::clock: renderClock(x_offset, now_ms); break;
    case View::stopwatch: renderStopwatch(x_offset, now_ms); break;
    case View::timer: renderTimer(x_offset, now_ms); break;
    case View::input_lab: renderInputLab(x_offset); break;
    case View::system: renderSystem(x_offset, now_ms); break;
    case View::about: renderAbout(x_offset); break;
    case View::snake: renderSnake(x_offset, now_ms); break;
    case View::dino: renderDino(x_offset, now_ms); break;
    case View::air_raid: renderAirRaid(x_offset); break;
    case View::tetris: renderTetris(x_offset); break;
    case View::pong: renderPong(x_offset); break;
    case View::piano: renderPiano(x_offset); break;
    default: break;
    }
}

void UiService::renderHeader(const std::int16_t x_offset, const etl::string_view title)
{
    canvas_.fillRectangle(x_offset, 0, display::Canvas::kWidth, 11);
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 4), 2, title, true);
    canvas_.pixel(static_cast<std::int16_t>(x_offset + 119), 3, display::PixelOperation::clear);
    canvas_.pixel(static_cast<std::int16_t>(x_offset + 122), 3, display::PixelOperation::clear);
    canvas_.pixel(static_cast<std::int16_t>(x_offset + 125), 3, display::PixelOperation::clear);
}

void UiService::renderFooter(const std::int16_t x_offset, const etl::string_view hint)
{
    canvas_.horizontalLine(x_offset, 54, display::Canvas::kWidth);
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 4), 56, hint);
}

void UiService::renderHome(const std::int16_t x_offset,
                           const std::uint32_t now_ms,
                           const bool interactive)
{
    const std::uint8_t selected = selections_[viewIndex(View::home)];
    if (interactive && !carousel_spring_.settled()) {
        const std::int16_t current_x = carousel_spring_.value();
        const std::int16_t previous_x = static_cast<std::int16_t>(
            current_x - carousel_direction_ * display::Canvas::kWidth);
        renderHomeCard(entryAt(View::home, carousel_previous_),
                       carousel_previous_,
                       static_cast<std::int16_t>(x_offset + previous_x),
                       now_ms);
        renderHomeCard(entryAt(View::home, selected),
                       selected,
                       static_cast<std::int16_t>(x_offset + current_x),
                       now_ms);
    } else {
        renderHomeCard(entryAt(View::home, selected), selected, x_offset, now_ms);
    }
}

void UiService::renderHomeCard(const MenuEntry& entry,
                               const std::uint8_t index,
                               const std::int16_t x_offset,
                               const std::uint32_t now_ms)
{
    const MenuDefinition* const home = menuFor(View::home);
    char position[6]{};
    formatPosition(index, home->count, position);

    // One almost-full-screen surface replaces the old header/card/footer
    // sandwich. Every vertical band now carries useful category information.
    canvas_.roundedRectangle(static_cast<std::int16_t>(x_offset + 2), 1, 124, 62, 3);
    renderHomeHeader(x_offset, now_ms);
    canvas_.fillRoundedRectangle(static_cast<std::int16_t>(x_offset + 92), 2, 32, 9, 2);
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 93), 3, position, true);
    canvas_.horizontalLine(static_cast<std::int16_t>(x_offset + 4), 12, 120);

    canvas_.roundedRectangle(static_cast<std::int16_t>(x_offset + 7), 16, 34, 27, 3);
    renderIcon(entry.icon, static_cast<std::int16_t>(x_offset + 12), 21);

    const std::int16_t title_width = static_cast<std::int16_t>(textWidth(entry.label) + 8);
    canvas_.fillRoundedRectangle(static_cast<std::int16_t>(x_offset + 45),
                                 17,
                                 title_width,
                                 11,
                                 2);
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 49), 19, entry.label, true);
    canvas_.horizontalLine(static_cast<std::int16_t>(x_offset + 46), 30, 77);
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 46), 33, entry.subtitle);

    canvas_.horizontalLine(static_cast<std::int16_t>(x_offset + 4), 47, 120);
    for (std::uint8_t dot = 0U; dot < home->count; ++dot) {
        const std::int16_t dot_x = static_cast<std::int16_t>(
            x_offset + 8 + static_cast<std::int16_t>(dot) * 7);
        if (dot == index) {
            canvas_.fillRoundedRectangle(dot_x, 54, 5, 3, 1);
        } else {
            canvas_.pixel(static_cast<std::int16_t>(dot_x + 2), 55);
        }
    }
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 42), 52, "<> MOVE");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 92), 52, "ENTER");
}

void UiService::refreshClock(const std::uint32_t now_ms)
{
    if (clock_editing_ ||
        (clock_snapshot_valid_ &&
         static_cast<std::int32_t>(now_ms - clock_next_refresh_ms_) < 0)) {
        return;
    }
    clock_snapshot_valid_ = calendar_.read(clock_snapshot_);
    // Polling four times per second makes the visible second rollover prompt,
    // while the OLED shadow still sends data only when the text actually changes.
    clock_next_refresh_ms_ = now_ms + 250U;
}

void UiService::renderHomeHeader(const std::int16_t x_offset,
                                 const std::uint32_t now_ms)
{
    switch (home_header_mode_) {
    case storage::HomeHeaderMode::time: {
        refreshClock(now_ms);
        if (!clock_snapshot_valid_) {
            canvas_.drawText(static_cast<std::int16_t>(x_offset + 7), 3, "RTC ERR");
            return;
        }
        char time_text[9]{};
        formatTime(static_cast<std::uint32_t>(clock_snapshot_.hours) * 3600U +
                       static_cast<std::uint32_t>(clock_snapshot_.minutes) * 60U +
                       clock_snapshot_.seconds,
                   time_text);
        canvas_.drawText(static_cast<std::int16_t>(x_offset + 7), 3, time_text);
        break;
    }
    case storage::HomeHeaderMode::date: {
        refreshClock(now_ms);
        if (!clock_snapshot_valid_) {
            canvas_.drawText(static_cast<std::int16_t>(x_offset + 7), 3, "RTC ERR");
            return;
        }
        char date_text[11]{};
        formatDate(clock_snapshot_.date, date_text);
        canvas_.drawText(static_cast<std::int16_t>(x_offset + 7), 3, date_text);
        break;
    }
    case storage::HomeHeaderMode::pet:
        renderHomePet(x_offset, now_ms);
        break;
    case storage::HomeHeaderMode::title:
        canvas_.drawText(static_cast<std::int16_t>(x_offset + 7), 3, "GAMEBOX");
        break;
    }
}

void UiService::renderHomePet(const std::int16_t x_offset,
                              const std::uint32_t now_ms)
{
    constexpr std::uint32_t travel_steps = 36U;
    std::uint32_t frame_period_ms = 0U;
    if (motion_ == MotionLevel::full) {
        frame_period_ms = 250U;
    } else if (motion_ == MotionLevel::reduced) {
        frame_period_ms = 500U;
    }

    std::uint32_t phase = travel_steps / 2U;
    std::uint32_t animation_frame = 0U;
    if (frame_period_ms != 0U) {
        const std::uint32_t tick = now_ms / frame_period_ms;
        phase = tick % (travel_steps * 2U);
        animation_frame = tick & 1U;
    }
    const bool facing_right = phase < travel_steps;
    const std::uint32_t step =
        facing_right ? phase : (travel_steps * 2U - 1U - phase);
    const std::int16_t pet_x = static_cast<std::int16_t>(
        x_offset + 7 + static_cast<std::int16_t>(step * 2U));

    for (std::uint8_t row = 0U; row < 9U; ++row) {
        const std::uint16_t pixels = kPetFrames[animation_frame][row];
        for (std::uint8_t column = 0U; column < 12U; ++column) {
            if ((pixels & static_cast<std::uint16_t>(1U << column)) == 0U) {
                continue;
            }
            const std::uint8_t mirrored =
                facing_right ? column : static_cast<std::uint8_t>(11U - column);
            canvas_.pixel(static_cast<std::int16_t>(pet_x + mirrored),
                          static_cast<std::int16_t>(2 + row));
        }
    }
}

void UiService::renderList(const View view,
                           const std::int16_t x_offset,
                           const bool interactive)
{
    const MenuDefinition* const menu = menuFor(view);
    const std::size_t state_index = viewIndex(view);
    const std::uint8_t selected = selections_[state_index];
    char position[6]{};
    formatPosition(selected, menu->count, position);

    const std::int16_t scroll_y = interactive ? scroll_spring_.value() : scrollTargetY(view);
    for (std::uint8_t item_index = 0U; item_index < menu->count; ++item_index) {
        const std::int16_t y = static_cast<std::int16_t>(
            kFirstRowY + static_cast<std::int16_t>(item_index) * kRowHeight + scroll_y);
        if (y <= -8 || y >= display::Canvas::kHeight) {
            continue;
        }
        const MenuEntry& entry = menu->entries[item_index];
        canvas_.drawText(static_cast<std::int16_t>(x_offset + 8), y, entry.label);
        if (view == View::settings) {
            const char* const value = settingValue(entry.action);
            std::size_t length = 0U;
            while (value[length] != '\0') { ++length; }
            const std::int16_t value_x = static_cast<std::int16_t>(
                x_offset + 120 - static_cast<std::int16_t>(length * 6U));
            canvas_.drawText(value_x, y, value);
        }
    }

    const std::int16_t highlight_y =
        interactive ? selection_spring_.value() : selectionTargetY(view);
    const std::int16_t highlight_width =
        interactive ? selection_width_spring_.value() : selectionTargetWidth(view);
    canvas_.fillRoundedRectangle(static_cast<std::int16_t>(x_offset + 3),
                                 highlight_y,
                                 highlight_width,
                                 kSelectionHeight,
                                 2,
                                 display::PixelOperation::invert);

    if (menu->count > 1U) {
        canvas_.verticalLine(static_cast<std::int16_t>(x_offset + 125), 15, 46);
        const std::uint16_t position_y = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(selected) * 42U /
            static_cast<std::uint16_t>(menu->count - 1U));
        canvas_.fillRoundedRectangle(static_cast<std::int16_t>(x_offset + 123),
                                     static_cast<std::int16_t>(15 + position_y),
                                     5,
                                     4,
                                     1);
    }

    // Redraw an opaque title rail last so rows moving on the spring are
    // cleanly clipped at its lower edge.
    canvas_.fillRectangle(x_offset,
                          0,
                          display::Canvas::kWidth,
                          12,
                          display::PixelOperation::clear);
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 4), 2, menu->title);
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 96), 2, position);
    canvas_.horizontalLine(x_offset, 11, display::Canvas::kWidth);
}

void UiService::renderClock(const std::int16_t x_offset, const std::uint32_t now_ms)
{
    refreshClock(now_ms);
    const platform::DateTime& shown = clock_editing_ ? clock_edit_ : clock_snapshot_;
    char time_text[9]{};
    formatTime(static_cast<std::uint32_t>(shown.hours) * 3600U +
                   static_cast<std::uint32_t>(shown.minutes) * 60U + shown.seconds,
               time_text);
    char date_text[11]{};
    formatDate(shown.date, date_text);

    renderHeader(x_offset, clock_editing_ ? "CLOCK SET" : "CLOCK");
    if (clock_snapshot_valid_ || clock_editing_) {
        canvas_.drawTextScaled(static_cast<std::int16_t>(x_offset + 16), 17, time_text, 2U);
        canvas_.horizontalLine(static_cast<std::int16_t>(x_offset + 22), 38, 84);
        canvas_.drawText(static_cast<std::int16_t>(x_offset + 34), 43, date_text);
    } else {
        canvas_.drawText(static_cast<std::int16_t>(x_offset + 37), 27, "RTC ERROR");
    }

    if (clock_editing_ && ((now_ms / 350U) & 1U) == 0U) {
        constexpr std::int16_t field_x[] = {16, 52, 34, 64, 82};
        constexpr std::int16_t field_width[] = {22, 22, 24, 12, 12};
        constexpr std::int16_t field_y[] = {35, 35, 52, 52, 52};
        canvas_.horizontalLine(static_cast<std::int16_t>(x_offset + field_x[clock_field_]),
                               field_y[clock_field_],
                               field_width[clock_field_]);
    }
    renderFooter(x_offset,
                 clock_editing_ ? "<> FIELD UP/DN ENTER" : "ENTER SET  BACK");
}

void UiService::renderStopwatch(const std::int16_t x_offset, const std::uint32_t now_ms)
{
    std::uint32_t elapsed_ms = stopwatch_accumulated_ms_;
    if (stopwatch_running_) {
        elapsed_ms += now_ms - stopwatch_started_ms_;
    }
    char time_text[9]{};
    formatTime(elapsed_ms / 1000U, time_text);
    renderHeader(x_offset, stopwatch_running_ ? "STOPWATCH  RUN" : "STOPWATCH");
    canvas_.drawTextScaled(static_cast<std::int16_t>(x_offset + 16), 18, time_text, 2U);
    char tenths[] = ".0 s";
    tenths[1] = static_cast<char>('0' + ((elapsed_ms / 100U) % 10U));
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 52), 39, tenths);
    renderFooter(x_offset, "ENTER START  FUNC RESET");
}

void UiService::renderTimer(const std::int16_t x_offset, const std::uint32_t now_ms)
{
    std::uint32_t seconds = countdown_seconds_;
    if (countdown_running_) {
        const std::int32_t remaining = static_cast<std::int32_t>(countdown_deadline_ms_ - now_ms);
        seconds = remaining > 0 ? static_cast<std::uint32_t>(remaining + 999) / 1000U : 0U;
    }
    char time_text[9]{};
    formatTime(seconds, time_text);
    renderHeader(x_offset, countdown_running_ ? "COUNTDOWN  RUN" : "COUNTDOWN");
    canvas_.drawTextScaled(static_cast<std::int16_t>(x_offset + 16), 18, time_text, 2U);
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 27), 39,
                     countdown_running_ ? "ENTER PAUSE" : "DPAD ADJUST");
    renderFooter(x_offset, "ENTER GO  FUNC 05:00");
}

void UiService::renderInputLab(const std::int16_t x_offset)
{
    renderHeader(x_offset, "INPUT LAB");
    if (event_count_ == 0U) {
        canvas_.drawText(static_cast<std::int16_t>(x_offset + 31), 27, "PRESS A KEY");
        renderFooter(x_offset, "CLICK  DOUBLE  LONG");
        return;
    }
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 5), 15, "KEY");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 47), 15, buttonName(last_event_.button));
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 5), 26, "EVENT");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 47), 26, eventName(last_event_.type));
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 5), 37, "HELD");
    char held[11]{};
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 47), 37,
                     unsignedText(last_event_.held_ms, held));
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 89), 37, "ms");
    renderFooter(x_offset, "TRY CLICK DOUBLE LONG");
}

void UiService::renderSystem(const std::int16_t x_offset, const std::uint32_t now_ms)
{
    if (system_next_stack_sample_ms_ == 0U ||
        static_cast<std::int32_t>(now_ms - system_next_stack_sample_ms_) >= 0) {
        const std::uint32_t input_free = input_.stackHeadroomBytes();
        const std::uint32_t audio_free = audio_.stackHeadroomBytes();
        const std::uint32_t diagnostics_free = diagnostics_.stackHeadroomBytes();
        const std::uint32_t ui_free = stackHeadroomBytes();
        system_stack_free_bytes_ = input_free < audio_free ? input_free : audio_free;
        if (diagnostics_free < system_stack_free_bytes_) {
            system_stack_free_bytes_ = diagnostics_free;
        }
        if (ui_free < system_stack_free_bytes_) {
            system_stack_free_bytes_ = ui_free;
        }
        system_next_stack_sample_ms_ = now_ms + 1000U;
    }

    renderHeader(x_offset, "SYSTEM");
    char value[11]{};
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 5), 14, "STACK FREE");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 65), 14,
                     unsignedText(system_stack_free_bytes_, value));
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 110), 14, "B");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 5), 24, "DROP I:");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 47), 24,
                     unsignedText(input_.droppedEventCount(), value));
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 77), 24, "U:");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 92), 24,
                     unsignedText(diagnostics_.droppedEventCount(), value));
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 5), 34, "ERR  O:");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 47), 34,
                     unsignedText(display_.errorCount(), value));
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 77), 34, "U:");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 92), 34,
                     unsignedText(diagnostics_.errorCount(), value));
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 5), 44, "OLED DMA TX");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 83), 44,
                     unsignedText(display_.dmaTransferCount(), value));
    renderFooter(x_offset, "NO HEAP  FREERTOS 11.3");
}

void UiService::renderAbout(const std::int16_t x_offset)
{
    renderHeader(x_offset, "ABOUT");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 20), 15, "STM32 GAMEBOX");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 29), 26, "F103C8T6");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 17), 37, "C++20 + RTOS");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 23), 47, "FW 2.0.0-dev");
    renderFooter(x_offset, "OPEN ARCHITECTURE");
}

void UiService::renderSnake(const std::int16_t x_offset, const std::uint32_t now_ms)
{
    canvas_.rectangle(static_cast<std::int16_t>(x_offset + 2), 12, 124, 51);
    char score[11]{};
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 3), 2, "SNAKE");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 86), 2, "S:");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 100), 2,
                     unsignedText(snake_.score(), score));
    for (std::size_t index = 0U; index < snake_.length(); ++index) {
        const games::SnakeGame::Point& point = snake_.segment(index);
        const std::int16_t x = static_cast<std::int16_t>(
            x_offset + 4 + static_cast<std::int16_t>(point.x * 4U));
        const std::int16_t y = static_cast<std::int16_t>(14 + point.y * 4U);
        if (index == 0U) {
            canvas_.roundedRectangle(x, y, 3, 3, 1);
        } else {
            canvas_.fillRectangle(x, y, 3, 3);
        }
    }
    const games::SnakeGame::Point& food = snake_.food();
    if (((now_ms / 220U) & 1U) == 0U) {
        canvas_.fillRoundedRectangle(static_cast<std::int16_t>(
                                         x_offset + 4 +
                                         static_cast<std::int16_t>(food.x * 4U)),
                                     static_cast<std::int16_t>(14 + food.y * 4U),
                                     3,
                                     3,
                                     1);
    }
    renderGameOverlay(x_offset, snake_.state());
}

void UiService::renderGameOverlay(const std::int16_t x_offset,
                                  const games::GamePhase state,
                                  const etl::string_view ready_hint)
{
    if (state == games::GamePhase::playing) {
        return;
    }
    canvas_.fillRoundedRectangle(static_cast<std::int16_t>(x_offset + 20), 24, 88, 24, 3);
    const etl::string_view title =
        state == games::GamePhase::ready ? etl::string_view{"READY"} :
                                          etl::string_view{"GAME OVER"};
    const std::int16_t title_x = static_cast<std::int16_t>(
        x_offset + (display::Canvas::kWidth - static_cast<std::int16_t>(title.size() * 6U)) / 2);
    const std::int16_t hint_x = static_cast<std::int16_t>(
        x_offset + (display::Canvas::kWidth - static_cast<std::int16_t>(ready_hint.size() * 6U)) / 2);
    canvas_.drawText(title_x, 28, title, true);
    canvas_.drawText(hint_x, 38, ready_hint, true);
}

void UiService::renderDino(const std::int16_t x_offset, const std::uint32_t now_ms)
{
    char score[11]{};
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 3), 2, "DINO");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 86), 2, "S:");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 100), 2,
                     unsignedText(dino_.score(), score));
    canvas_.horizontalLine(x_offset, games::DinoGame::kGroundY + 1,
                           display::Canvas::kWidth);
    for (std::int16_t dash = 0; dash < display::Canvas::kWidth; dash += 9) {
        const std::int16_t shifted = static_cast<std::int16_t>(
            (dash - static_cast<std::int16_t>((now_ms / 45U) % 9U) +
             display::Canvas::kWidth) % display::Canvas::kWidth);
        canvas_.horizontalLine(static_cast<std::int16_t>(x_offset + shifted), 62, 4);
    }

    const std::int16_t dino_bottom = static_cast<std::int16_t>(
        games::DinoGame::kGroundY - dino_.jumpHeight());
    const std::int16_t dino_x = static_cast<std::int16_t>(x_offset + games::DinoGame::kDinoX);
    canvas_.fillRoundedRectangle(dino_x,
                                 static_cast<std::int16_t>(dino_bottom - 8),
                                 8,
                                 8,
                                 2);
    canvas_.fillRectangle(static_cast<std::int16_t>(dino_x + 5),
                          static_cast<std::int16_t>(dino_bottom - 11),
                          7,
                          5);
    canvas_.pixel(static_cast<std::int16_t>(dino_x + 10),
                  static_cast<std::int16_t>(dino_bottom - 10),
                  display::PixelOperation::clear);
    canvas_.verticalLine(static_cast<std::int16_t>(dino_x + 1), dino_bottom, 2);
    canvas_.verticalLine(static_cast<std::int16_t>(dino_x + 6), dino_bottom, 2);

    for (std::size_t index = 0U; index < games::DinoGame::kObstacleCount; ++index) {
        const games::DinoGame::Obstacle& obstacle = dino_.obstacle(index);
        if (!obstacle.active) {
            continue;
        }
        const std::int16_t obstacle_y = static_cast<std::int16_t>(
            games::DinoGame::kGroundY - obstacle.height + 1U);
        canvas_.fillRectangle(static_cast<std::int16_t>(x_offset + obstacle.x),
                              obstacle_y,
                              obstacle.width,
                              obstacle.height);
        canvas_.horizontalLine(static_cast<std::int16_t>(x_offset + obstacle.x - 2),
                               static_cast<std::int16_t>(obstacle_y + 4),
                               3);
    }
    renderGameOverlay(x_offset, dino_.state(), "JUMP TO PLAY");
}

void UiService::renderAirRaid(const std::int16_t x_offset)
{
    char value[11]{};
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 2), 2, "AIR RAID");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 60), 2, "S:");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 74), 2,
                     unsignedText(air_raid_.score(), value));
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 101), 2, "L:");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 115), 2,
                     unsignedText(air_raid_.lives(), value));
    canvas_.horizontalLine(x_offset, 10, display::Canvas::kWidth);

    const std::int16_t player_y = air_raid_.playerY();
    canvas_.line(static_cast<std::int16_t>(x_offset + 5),
                 static_cast<std::int16_t>(player_y + 3),
                 static_cast<std::int16_t>(x_offset + 16),
                 player_y);
    canvas_.line(static_cast<std::int16_t>(x_offset + 5),
                 static_cast<std::int16_t>(player_y + 3),
                 static_cast<std::int16_t>(x_offset + 16),
                 static_cast<std::int16_t>(player_y + 6));
    canvas_.horizontalLine(static_cast<std::int16_t>(x_offset + 5),
                           static_cast<std::int16_t>(player_y + 3),
                           12);
    canvas_.verticalLine(static_cast<std::int16_t>(x_offset + 8), player_y, 7);

    for (std::size_t index = 0U; index < games::AirRaidGame::kBulletCount; ++index) {
        const games::AirRaidGame::Entity& bullet = air_raid_.bullet(index);
        if (bullet.active) {
            canvas_.horizontalLine(static_cast<std::int16_t>(x_offset + bullet.x),
                                   bullet.y,
                                   3);
        }
    }
    for (std::size_t index = 0U; index < games::AirRaidGame::kEnemyCount; ++index) {
        const games::AirRaidGame::Entity& enemy = air_raid_.enemy(index);
        if (!enemy.active) {
            continue;
        }
        const std::int16_t enemy_x = static_cast<std::int16_t>(x_offset + enemy.x);
        canvas_.roundedRectangle(enemy_x, enemy.y, 9, 7, 2);
        canvas_.horizontalLine(static_cast<std::int16_t>(enemy_x - 3),
                               static_cast<std::int16_t>(enemy.y + 3),
                               4);
        canvas_.pixel(static_cast<std::int16_t>(enemy_x + 2),
                      static_cast<std::int16_t>(enemy.y + 2));
    }
    renderGameOverlay(x_offset, air_raid_.state());
}

void UiService::renderTetris(const std::int16_t x_offset)
{
    constexpr std::int16_t cell = 4;
    const std::int16_t board_x = static_cast<std::int16_t>(x_offset + 2);
    canvas_.rectangle(board_x, 1, 42, 62);
    for (std::uint8_t y = 0U; y < games::TetrisGame::kRows; ++y) {
        for (std::uint8_t x = 0U; x < games::TetrisGame::kColumns; ++x) {
            if (!tetris_.settled(x, y) && !tetris_.active(x, y)) {
                continue;
            }
            canvas_.fillRectangle(
                static_cast<std::int16_t>(board_x + 1 + static_cast<std::int16_t>(x) * cell),
                static_cast<std::int16_t>(2 + static_cast<std::int16_t>(y) * cell),
                3,
                3);
        }
    }

    char value[11]{};
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 49), 3, "TETRIS");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 49), 15, "SCORE");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 49), 24,
                     unsignedText(tetris_.score(), value));
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 49), 35, "NEXT");
    const std::uint8_t next = tetris_.nextPiece();
    for (std::uint8_t y = 0U; y < 4U; ++y) {
        for (std::uint8_t x = 0U; x < 4U; ++x) {
            if (games::TetrisGame::pieceCell(next, 0U, x, y)) {
                canvas_.fillRectangle(
                    static_cast<std::int16_t>(x_offset + 54 + static_cast<std::int16_t>(x) * cell),
                    static_cast<std::int16_t>(44 + static_cast<std::int16_t>(y) * cell),
                    3,
                    3);
            }
        }
    }
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 82), 44, "UP DROP");
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 82), 54, "J/F ROT");
    renderGameOverlay(x_offset, tetris_.state());
}

void UiService::renderPong(const std::int16_t x_offset)
{
    canvas_.fillRectangle(x_offset, 0, display::Canvas::kWidth, 9);
    canvas_.drawText(static_cast<std::int16_t>(x_offset + 46), 1, "PONG 2P", true);
    canvas_.horizontalLine(x_offset, 10, display::Canvas::kWidth);
    canvas_.horizontalLine(x_offset, 63, display::Canvas::kWidth);
    for (std::int16_t y = 13; y < 61; y += 6) {
        canvas_.verticalLine(static_cast<std::int16_t>(x_offset + 64), y, 3);
    }
    canvas_.verticalLine(static_cast<std::int16_t>(x_offset + 7),
                         static_cast<std::int16_t>(pong_.leftY() - pong_.leftHalfLength()),
                         static_cast<std::int16_t>(pong_.leftHalfLength() * 2U + 1U));
    canvas_.verticalLine(static_cast<std::int16_t>(x_offset + 120),
                         static_cast<std::int16_t>(pong_.rightY() - pong_.rightHalfLength()),
                         static_cast<std::int16_t>(pong_.rightHalfLength() * 2U + 1U));
    canvas_.fillRoundedRectangle(static_cast<std::int16_t>(x_offset + pong_.ballX() - 1),
                                 static_cast<std::int16_t>(pong_.ballY() - 1),
                                 3,
                                 3,
                                 1);
    if (pong_.state() == games::PongGame::State::game_over) {
        const char* winner = pong_.winner() == 1U ? "LEFT WINS" : "RIGHT WINS";
        canvas_.fillRoundedRectangle(static_cast<std::int16_t>(x_offset + 23), 23, 82, 26, 3);
        canvas_.drawText(static_cast<std::int16_t>(x_offset + 37), 28, winner, true);
        canvas_.drawText(static_cast<std::int16_t>(x_offset + 28), 39, "ENTER RESTART", true);
    } else {
        renderGameOverlay(x_offset, pong_.state());
    }
}

void UiService::renderPiano(const std::int16_t x_offset)
{
    renderHeader(x_offset, "PIANO");
    constexpr char labels[] = {'C', 'D', 'E', 'F', 'G', 'A', 'B', 'C'};
    for (std::uint8_t index = 0U; index < 8U; ++index) {
        const std::int16_t key_x = static_cast<std::int16_t>(
            x_offset + 4 + static_cast<std::int16_t>(index) * 15);
        if (index == piano_note_) {
            canvas_.fillRoundedRectangle(key_x, 15, 14, 35, 2);
        } else {
            canvas_.roundedRectangle(key_x, 15, 14, 35, 2);
        }
        char label[2] = {labels[index], '\0'};
        canvas_.drawText(static_cast<std::int16_t>(key_x + 4),
                         38,
                         label,
                         index == piano_note_);
    }
    renderFooter(x_offset, "LONG BACK TO EXIT");
}

void UiService::renderIcon(const Icon icon, const std::int16_t x, const std::int16_t y)
{
    switch (icon) {
    case Icon::gamepad:
        canvas_.roundedRectangle(x, y, 24, 16, 3);
        canvas_.horizontalLine(static_cast<std::int16_t>(x + 4), static_cast<std::int16_t>(y + 8), 7);
        canvas_.verticalLine(static_cast<std::int16_t>(x + 7), static_cast<std::int16_t>(y + 5), 7);
        canvas_.fillRoundedRectangle(static_cast<std::int16_t>(x + 16),
                                     static_cast<std::int16_t>(y + 5), 3, 3, 1);
        canvas_.fillRoundedRectangle(static_cast<std::int16_t>(x + 20),
                                     static_cast<std::int16_t>(y + 9), 3, 3, 1);
        break;
    case Icon::clock:
    case Icon::stopwatch:
    case Icon::timer:
        canvas_.circle(static_cast<std::int16_t>(x + 12), static_cast<std::int16_t>(y + 8), 8);
        canvas_.line(static_cast<std::int16_t>(x + 12), static_cast<std::int16_t>(y + 8),
                     static_cast<std::int16_t>(x + 12), static_cast<std::int16_t>(y + 3));
        canvas_.line(static_cast<std::int16_t>(x + 12), static_cast<std::int16_t>(y + 8),
                     static_cast<std::int16_t>(x + 17), static_cast<std::int16_t>(y + 11));
        break;
    case Icon::snake:
        canvas_.line(x, static_cast<std::int16_t>(y + 3),
                     static_cast<std::int16_t>(x + 6), static_cast<std::int16_t>(y + 12));
        canvas_.line(static_cast<std::int16_t>(x + 6), static_cast<std::int16_t>(y + 12),
                     static_cast<std::int16_t>(x + 13), static_cast<std::int16_t>(y + 3));
        canvas_.line(static_cast<std::int16_t>(x + 13), static_cast<std::int16_t>(y + 3),
                     static_cast<std::int16_t>(x + 22), static_cast<std::int16_t>(y + 10));
        canvas_.fillRoundedRectangle(static_cast<std::int16_t>(x + 20),
                                     static_cast<std::int16_t>(y + 8), 4, 4, 1);
        break;
    case Icon::dino:
        canvas_.fillRoundedRectangle(static_cast<std::int16_t>(x + 4),
                                     static_cast<std::int16_t>(y + 7), 11, 8, 2);
        canvas_.fillRectangle(static_cast<std::int16_t>(x + 12),
                              static_cast<std::int16_t>(y + 3), 9, 7);
        canvas_.pixel(static_cast<std::int16_t>(x + 18),
                      static_cast<std::int16_t>(y + 5),
                      display::PixelOperation::clear);
        canvas_.verticalLine(static_cast<std::int16_t>(x + 6),
                             static_cast<std::int16_t>(y + 14), 2);
        canvas_.verticalLine(static_cast<std::int16_t>(x + 13),
                             static_cast<std::int16_t>(y + 14), 2);
        break;
    case Icon::plane:
        canvas_.horizontalLine(x, static_cast<std::int16_t>(y + 8), 23);
        canvas_.line(static_cast<std::int16_t>(x + 5),
                     static_cast<std::int16_t>(y + 8),
                     static_cast<std::int16_t>(x + 17), y);
        canvas_.line(static_cast<std::int16_t>(x + 5),
                     static_cast<std::int16_t>(y + 8),
                     static_cast<std::int16_t>(x + 17),
                     static_cast<std::int16_t>(y + 15));
        break;
    case Icon::tetris:
        canvas_.fillRectangle(x, static_cast<std::int16_t>(y + 8), 5, 5);
        canvas_.fillRectangle(static_cast<std::int16_t>(x + 6),
                              static_cast<std::int16_t>(y + 8), 5, 5);
        canvas_.fillRectangle(static_cast<std::int16_t>(x + 12),
                              static_cast<std::int16_t>(y + 8), 5, 5);
        canvas_.fillRectangle(static_cast<std::int16_t>(x + 6),
                              static_cast<std::int16_t>(y + 2), 5, 5);
        break;
    case Icon::pong:
        canvas_.verticalLine(x, static_cast<std::int16_t>(y + 2), 13);
        canvas_.verticalLine(static_cast<std::int16_t>(x + 23),
                             static_cast<std::int16_t>(y + 2), 13);
        canvas_.circle(static_cast<std::int16_t>(x + 12),
                       static_cast<std::int16_t>(y + 8), 2);
        break;
    case Icon::piano:
        for (std::int16_t key = 0; key < 4; ++key) {
            canvas_.rectangle(static_cast<std::int16_t>(x + key * 6), y, 6, 16);
        }
        break;
    case Icon::settings:
    case Icon::motion:
        canvas_.circle(static_cast<std::int16_t>(x + 12), static_cast<std::int16_t>(y + 8), 7);
        canvas_.circle(static_cast<std::int16_t>(x + 12), static_cast<std::int16_t>(y + 8), 3);
        canvas_.horizontalLine(x, static_cast<std::int16_t>(y + 8), 24);
        canvas_.verticalLine(static_cast<std::int16_t>(x + 12), y, 16);
        break;
    case Icon::tools:
        canvas_.line(static_cast<std::int16_t>(x + 2), static_cast<std::int16_t>(y + 2),
                     static_cast<std::int16_t>(x + 21), static_cast<std::int16_t>(y + 14));
        canvas_.line(static_cast<std::int16_t>(x + 21), static_cast<std::int16_t>(y + 2),
                     static_cast<std::int16_t>(x + 2), static_cast<std::int16_t>(y + 14));
        canvas_.circle(static_cast<std::int16_t>(x + 3), static_cast<std::int16_t>(y + 2), 2);
        break;
    case Icon::buttons:
        canvas_.roundedRectangle(x, y, 24, 16, 3);
        canvas_.drawText(static_cast<std::int16_t>(x + 3), static_cast<std::int16_t>(y + 4), "KEY");
        break;
    case Icon::speaker:
        canvas_.fillRectangle(x, static_cast<std::int16_t>(y + 6), 5, 5);
        canvas_.line(static_cast<std::int16_t>(x + 5), static_cast<std::int16_t>(y + 6),
                     static_cast<std::int16_t>(x + 11), static_cast<std::int16_t>(y + 2));
        canvas_.line(static_cast<std::int16_t>(x + 5), static_cast<std::int16_t>(y + 10),
                     static_cast<std::int16_t>(x + 11), static_cast<std::int16_t>(y + 14));
        canvas_.circle(static_cast<std::int16_t>(x + 11), static_cast<std::int16_t>(y + 8), 7);
        break;
    case Icon::brightness:
        canvas_.circle(static_cast<std::int16_t>(x + 12), static_cast<std::int16_t>(y + 8), 5);
        canvas_.horizontalLine(x, static_cast<std::int16_t>(y + 8), 24);
        canvas_.verticalLine(static_cast<std::int16_t>(x + 12), y, 16);
        break;
    case Icon::chip:
        canvas_.rectangle(static_cast<std::int16_t>(x + 4),
                          static_cast<std::int16_t>(y + 2), 16, 12);
        for (std::int16_t pin = 0; pin < 4; ++pin) {
            canvas_.horizontalLine(x, static_cast<std::int16_t>(y + 4 + pin * 3), 4);
            canvas_.horizontalLine(static_cast<std::int16_t>(x + 20),
                                   static_cast<std::int16_t>(y + 4 + pin * 3), 4);
        }
        break;
    case Icon::info:
        canvas_.circle(static_cast<std::int16_t>(x + 12), static_cast<std::int16_t>(y + 8), 8);
        canvas_.drawText(static_cast<std::int16_t>(x + 9), static_cast<std::int16_t>(y + 4), "i");
        break;
    }
}

void UiService::renderToast(const std::uint32_t now_ms)
{
    if (toast_.empty() || static_cast<std::int32_t>(now_ms - toast_until_ms_) >= 0) {
        return;
    }
    canvas_.fillRoundedRectangle(3, 43, 122, 18, 3);
    const std::int16_t width = static_cast<std::int16_t>(toast_.size() * 6U);
    const std::int16_t x = static_cast<std::int16_t>((128 - width) / 2);
    canvas_.drawText(x, 48, toast_, true);
}

} // namespace gamebox::ui
