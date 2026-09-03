#include <cstdint>
#include <iostream>
#include <limits>

#include "app/app_manager.hpp"
#include "display/canvas.hpp"
#include "games/air_raid_game.hpp"
#include "games/dino_game.hpp"
#include "games/pong_game.hpp"
#include "games/snake_game.hpp"
#include "games/tetris_game.hpp"
#include "input/button_engine.hpp"
#include "storage/settings_codec.hpp"
#include "storage/calendar_codec.hpp"
#include "ui/menu_model.hpp"
#include "ui/tween.hpp"

namespace {

int failures = 0;

void check(const bool condition, const char* const message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct EventLog {
    gamebox::input::ButtonEvent events[32]{};
    std::size_t count{0U};

    static void append(void* const context, const gamebox::input::ButtonEvent& event)
    {
        auto& self = *static_cast<EventLog*>(context);
        if (self.count < (sizeof(self.events) / sizeof(self.events[0]))) {
            self.events[self.count++] = event;
        }
    }

    std::size_t countType(const gamebox::input::ButtonEventType type) const
    {
        std::size_t result = 0U;
        for (std::size_t index = 0U; index < count; ++index) {
            if (events[index].type == type) {
                ++result;
            }
        }
        return result;
    }
};

void sample(gamebox::input::ButtonEngine& engine,
            EventLog& log,
            const bool pressed,
            const std::uint32_t now_ms)
{
    const auto mask = pressed ? gamebox::input::maskFor(gamebox::input::Button::enter) : 0U;
    engine.sample(static_cast<gamebox::input::ButtonMask>(mask), now_ms, EventLog::append, &log);
}

void testDebounceAndClick()
{
    gamebox::input::ButtonEngine engine;
    EventLog log;
    sample(engine, log, true, 0U);
    sample(engine, log, false, 5U);
    sample(engine, log, true, 10U);
    sample(engine, log, true, 29U);
    check(log.count == 0U, "bounce must not create an event");
    sample(engine, log, true, 30U);
    check(log.countType(gamebox::input::ButtonEventType::pressed) == 1U,
          "debounced press must be emitted exactly once");

    sample(engine, log, false, 100U);
    sample(engine, log, false, 120U);
    sample(engine, log, false, 399U);
    check(log.countType(gamebox::input::ButtonEventType::click) == 0U,
          "single click must wait through the double-click window");
    sample(engine, log, false, 400U);
    check(log.countType(gamebox::input::ButtonEventType::released) == 1U,
          "release must be emitted");
    check(log.countType(gamebox::input::ButtonEventType::click) == 1U,
          "single click must be emitted after the double-click window");
}

void testDoubleClick()
{
    gamebox::input::ButtonEngine engine;
    EventLog log;
    sample(engine, log, true, 0U);
    sample(engine, log, true, 20U);
    sample(engine, log, false, 60U);
    sample(engine, log, false, 80U);
    sample(engine, log, true, 100U);
    sample(engine, log, true, 120U);
    sample(engine, log, false, 160U);
    sample(engine, log, false, 180U);
    sample(engine, log, false, 500U);
    check(log.countType(gamebox::input::ButtonEventType::pressed) == 2U,
          "double click needs two presses");
    check(log.countType(gamebox::input::ButtonEventType::double_click) == 1U,
          "double click must be classified once");
    check(log.countType(gamebox::input::ButtonEventType::click) == 0U,
          "double click must suppress single-click events");
}

void testLongPressAndRepeat()
{
    gamebox::input::ButtonEngine engine;
    EventLog log;
    sample(engine, log, true, 0U);
    sample(engine, log, true, 20U);
    sample(engine, log, true, 669U);
    sample(engine, log, true, 670U);
    sample(engine, log, true, 779U);
    sample(engine, log, true, 780U);
    sample(engine, log, false, 800U);
    sample(engine, log, false, 820U);
    sample(engine, log, false, 1200U);
    check(log.countType(gamebox::input::ButtonEventType::long_press) == 1U,
          "long press must fire at its threshold");
    check(log.countType(gamebox::input::ButtonEventType::repeat) == 1U,
          "held button must repeat after its repeat delay");
    check(log.countType(gamebox::input::ButtonEventType::click) == 0U,
          "long press must not also become a click");
}

void testTickWrap()
{
    gamebox::input::ButtonEngine engine;
    EventLog log;
    constexpr std::uint32_t near_wrap = std::numeric_limits<std::uint32_t>::max() - 10U;
    sample(engine, log, true, near_wrap);
    sample(engine, log, true, 9U);
    check(log.countType(gamebox::input::ButtonEventType::pressed) == 1U,
          "debounce timing must survive uint32 wrap-around");
}

void testTween()
{
    gamebox::ui::Tween tween;
    tween.start(0, 128, 100U, 200U);
    check(tween.value(100U) == 0, "tween must start at its source");
    check(tween.value(200U) > 64, "ease-out tween must lead linear motion at midpoint");
    check(tween.value(300U) == 128, "tween must finish exactly at its target");
    check(!tween.active(300U), "finished tween must be inactive");

    tween.start(0, 100, 0U, gamebox::ui::Tween::kMaximumDurationMs + 1U);
    check(!tween.active(gamebox::ui::Tween::kMaximumDurationMs),
          "tween duration must be bounded to protect fixed-point arithmetic");

    gamebox::ui::Spring spring(0);
    spring.setTarget(100);
    bool overshot = false;
    for (std::uint16_t step = 0U; step < 180U; ++step) {
        spring.step(gamebox::ui::SpringSpeed::fast);
        if (spring.value() > 100) {
            overshot = true;
        }
    }
    check(overshot, "fast spring must retain the Embassy motion overshoot");
    check(spring.value() == 100 && spring.settled(),
          "spring must converge exactly without floating point");

    spring.snapTo(-12);
    spring.setTarget(87);
    spring.step(gamebox::ui::SpringSpeed::off);
    check(spring.value() == 87 && spring.settled(),
          "disabled motion must snap the spring to its target");
}

void testSnakeModel()
{
    gamebox::games::SnakeGame snake;
    snake.reset(1234U);
    const auto start = snake.segment(0U);
    snake.start();
    snake.update(1000U);
    snake.turn(gamebox::games::SnakeGame::Direction::down);
    snake.update(1150U);
    check(snake.segment(0U).x == start.x && snake.segment(0U).y == start.y + 1U,
          "snake must move in the requested direction");
    snake.turn(gamebox::games::SnakeGame::Direction::up);
    snake.update(1300U);
    check(snake.segment(0U).y == start.y + 2U,
          "snake must reject an immediate reverse turn");
}

void testDinoModel()
{
    gamebox::games::DinoGame dino;
    dino.reset(123U);
    check(dino.state() == gamebox::games::GamePhase::ready,
          "dino must reset to ready");
    dino.start();
    dino.jump();
    dino.update(1000U);
    dino.update(1045U);
    check(dino.jumpHeight() > 0U, "dino jump must advance without blocking");

    for (std::uint32_t tick = 0U;
         tick < 800U && dino.state() == gamebox::games::GamePhase::playing;
         ++tick) {
        dino.update(1090U + tick * 45U);
    }
    check(dino.state() == gamebox::games::GamePhase::game_over,
          "an unattended dino run must eventually collide");
}

void testAirRaidModel()
{
    gamebox::games::AirRaidGame air;
    air.reset(456U);
    air.start();
    check(air.fire() && air.fire() && air.fire(),
          "air raid must expose all three legacy bullet slots");
    check(!air.fire(), "air raid must enforce its three-bullet capacity");
    const auto x = air.bullet(0U).x;
    air.update(1000U);
    air.update(1033U);
    check(air.bullet(0U).x > x, "air-raid bullets must advance on timed updates");
    air.setVertical(-1);
    const auto y = air.playerY();
    air.update(1066U);
    check(air.playerY() < y, "air-raid continuous input must move the player");
}

void testTetrisModel()
{
    using gamebox::games::TetrisGame;
    for (std::uint8_t piece = 0U; piece < TetrisGame::kPieceCount; ++piece) {
        for (std::uint8_t rotation = 0U; rotation < 4U; ++rotation) {
            std::uint8_t cells = 0U;
            for (std::uint8_t y = 0U; y < 4U; ++y) {
                for (std::uint8_t x = 0U; x < 4U; ++x) {
                    if (TetrisGame::pieceCell(piece, rotation, x, y)) {
                        ++cells;
                    }
                }
            }
            check(cells == 4U, "every tetromino rotation must contain four cells");
        }
    }

    TetrisGame tetris;
    tetris.reset(789U);
    tetris.start();
    check(tetris.move(-1), "fresh tetris piece must move left");
    check(tetris.rotate(1), "fresh tetris piece must rotate with wall kicks");
    tetris.hardDrop();
    check(tetris.score() > 0U, "hard drop must award distance points");
    for (std::uint8_t piece = 0U;
         piece < 80U && tetris.state() == gamebox::games::GamePhase::playing;
         ++piece) {
        tetris.hardDrop();
    }
    check(tetris.state() == gamebox::games::GamePhase::game_over,
          "stacked tetris pieces must eventually end the game safely");
}

void testPongModel()
{
    gamebox::games::PongGame pong;
    pong.reset(321U);
    pong.start();
    const auto y = pong.leftY();
    pong.setLeftDirection(-1);
    pong.update(1000U);
    pong.update(1022U);
    check(pong.leftY() < y, "pong paddle input must be sampled continuously");
    check(pong.ballX() != 64, "pong ball must advance on a timed update");
}

void testGameMenu()
{
    using gamebox::ui::View;
    const gamebox::ui::MenuDefinition* const games = gamebox::ui::menuFor(View::games);
    check(games != nullptr, "games menu must exist");
    if (games == nullptr) {
        return;
    }
    check(games->count == 6U, "games menu must expose all six legacy game categories");
    constexpr View expected[] = {
        View::dino, View::snake, View::air_raid, View::tetris, View::pong, View::piano,
    };
    for (std::size_t index = 0U; index < (sizeof(expected) / sizeof(expected[0])); ++index) {
        check(gamebox::ui::entryAt(View::games, static_cast<std::uint8_t>(index)).target ==
                  expected[index],
              "games menu order and targets must remain complete");
        check(gamebox::ui::isGame(expected[index]),
              "every games-menu target must be classified as a game");
    }
}

void testSettingsMenu()
{
    using gamebox::ui::Action;
    using gamebox::ui::View;
    const gamebox::ui::MenuDefinition* const settings =
        gamebox::ui::menuFor(View::settings);
    check(settings != nullptr, "settings menu must exist");
    if (settings == nullptr) {
        return;
    }
    check(settings->count == 5U,
          "settings menu must expose the configurable home header");
    check(gamebox::ui::entryAt(View::settings, 3U).action ==
              Action::cycle_home_header,
          "home-header setting must remain device-accessible");
}

void testCanvas()
{
    gamebox::display::Canvas canvas;
    canvas.clearDirty();
    canvas.pixel(0, 0);
    canvas.pixel(127, 63);
    canvas.pixel(-1, 0);
    check(canvas.data()[0] == 0x01U, "canvas must map the top-left pixel");
    check(canvas.data()[1023] == 0x80U, "canvas must map the bottom-right pixel");
    check(canvas.dirtyPages() == 0x81U, "canvas must track dirty pages");
    canvas.drawText(3, 3, "A");
    check(canvas.dirtyPages() != 0U, "text rendering must mark changed pages");

    canvas.clear();
    canvas.clearDirty();
    canvas.line(std::numeric_limits<std::int16_t>::min(),
                std::numeric_limits<std::int16_t>::min(),
                0,
                0);
    check((canvas.data()[0] & 0x01U) != 0U,
          "extreme diagonal line must terminate and clip to the top-left pixel");

    canvas.clear();
    canvas.clearDirty();
    canvas.line(std::numeric_limits<std::int16_t>::min(),
                32,
                std::numeric_limits<std::int16_t>::max(),
                32);
    bool complete_row = true;
    for (std::size_t x = 0U; x < gamebox::display::Canvas::kWidth; ++x) {
        const std::size_t offset = 4U * gamebox::display::Canvas::kWidth + x;
        complete_row = complete_row && (canvas.data()[offset] & 0x01U) != 0U;
    }
    check(complete_row, "cross-screen extreme line must retain every visible pixel");

    canvas.clear();
    canvas.clearDirty();
    canvas.line(std::numeric_limits<std::int16_t>::min(),
                std::numeric_limits<std::int16_t>::min(),
                std::numeric_limits<std::int16_t>::min(),
                std::numeric_limits<std::int16_t>::max());
    check(canvas.dirtyPages() == 0U,
          "fully off-screen extreme line must be rejected without touching the canvas");
}

void testSettingsCodec()
{
    const gamebox::storage::SettingsData expected{
        false,
        2U,
        1U,
        gamebox::storage::HomeHeaderMode::pet,
    };
    const std::uint16_t encoded = gamebox::storage::SettingsCodec::encode(expected);
    const std::uint16_t checkword = gamebox::storage::SettingsCodec::checkword(encoded);
    gamebox::storage::SettingsData decoded{};
    check(gamebox::storage::SettingsCodec::decode(encoded, checkword, decoded),
          "valid persisted settings must decode");
    check(decoded.sound_enabled == expected.sound_enabled &&
              decoded.motion_level == expected.motion_level &&
              decoded.brightness_level == expected.brightness_level &&
              decoded.home_header_mode == expected.home_header_mode,
          "persisted settings must round-trip without loss");
    check(!gamebox::storage::SettingsCodec::decode(encoded,
                                                    static_cast<std::uint16_t>(checkword ^ 1U),
                                                    decoded),
          "corrupted persisted settings must be rejected");
    const std::uint16_t unknown_flags = static_cast<std::uint16_t>(encoded | 0x0080U);
    check(!gamebox::storage::SettingsCodec::decode(
              unknown_flags,
              gamebox::storage::SettingsCodec::checkword(unknown_flags),
              decoded),
              "unknown settings flags must be rejected by the current schema");

    const gamebox::storage::SettingsData legacy_compatible{false, 2U, 1U};
    const std::uint16_t legacy_encoded =
        gamebox::storage::SettingsCodec::encode(legacy_compatible);
    check((legacy_encoded & 0x0060U) == 0U,
          "zero-valued legacy header bits must select the default time header");
}

void testCalendarCodec()
{
    using gamebox::storage::CalendarCodec;
    using gamebox::storage::CalendarDate;

    const CalendarDate leap_day{24U, 2U, 29U};
    const std::uint16_t encoded = CalendarCodec::encode(leap_day);
    const std::uint16_t checkword = CalendarCodec::checkword(encoded);
    CalendarDate decoded{};
    check(CalendarCodec::decode(encoded, checkword, decoded) && decoded == leap_day,
          "valid calendar date must round-trip");
    check(!CalendarCodec::decode(encoded,
                                 static_cast<std::uint16_t>(checkword ^ 0x0100U),
                                 decoded),
          "corrupted calendar record must be rejected");
    check(!CalendarCodec::isValid({25U, 2U, 29U}),
          "non-leap-year February 29 must be rejected");
    check(!CalendarCodec::isValid({100U, 1U, 1U}),
          "calendar year must stay inside the STM32 HAL 2000-2099 range");

    CalendarDate year_end{23U, 12U, 31U};
    check(CalendarCodec::advance(year_end, 60U) &&
              year_end == CalendarDate{24U, 2U, 29U},
          "calendar arithmetic must cross year end and leap day");
    CalendarDate maximum{99U, 12U, 31U};
    check(!CalendarCodec::advance(maximum, 1U) &&
              maximum == CalendarDate{99U, 12U, 31U},
          "calendar arithmetic must saturate at the HAL range limit");
}

class FakeModule final : public gamebox::app::AppModule {
public:
    FakeModule(const char* const module_name,
               const int identifier,
               int* const log,
               std::size_t& log_size,
               const bool initialize_ok = true,
               const bool deinitialize_ok = true)
        : module_name_(module_name),
          identifier_(identifier),
          log_(log),
          log_size_(log_size),
          initialize_ok_(initialize_ok),
          deinitialize_ok_(deinitialize_ok)
    {
    }

    etl::string_view name() const override { return module_name_; }

protected:
    bool onInitialize() override
    {
        log_[log_size_++] = identifier_;
        return initialize_ok_;
    }

    bool onDeinitialize() override
    {
        log_[log_size_++] = -identifier_;
        return deinitialize_ok_;
    }

private:
    const char* module_name_;
    int identifier_;
    int* log_;
    std::size_t& log_size_;
    bool initialize_ok_;
    bool deinitialize_ok_;
};

void testModuleLifecycle()
{
    int log[16]{};
    std::size_t log_size = 0U;
    FakeModule first("first", 1, log, log_size);
    FakeModule second("second", 2, log, log_size);
    FakeModule third("third", 3, log, log_size);
    gamebox::app::AppManager manager;
    check(static_cast<bool>(manager.registerModule(first)), "first module must register");
    check(static_cast<bool>(manager.registerModule(second)), "second module must register");
    check(static_cast<bool>(manager.registerModule(third)), "third module must register");
    check(!static_cast<bool>(manager.registerModule(first)), "duplicate module must be rejected");
    check(static_cast<bool>(manager.initializeAll()), "all healthy modules must initialize");
    check(static_cast<bool>(manager.deinitializeAll()), "all healthy modules must deinitialize");
    const int expected[] = {1, 2, 3, -3, -2, -1};
    check(log_size == (sizeof(expected) / sizeof(expected[0])),
          "module lifecycle log must have all transitions");
    for (std::size_t index = 0U; index < log_size; ++index) {
        check(log[index] == expected[index], "modules must clean up in reverse order");
    }

    log_size = 0U;
    FakeModule ready("ready", 4, log, log_size);
    FakeModule failing("failing", 5, log, log_size, false, true);
    FakeModule never_started("never", 6, log, log_size);
    gamebox::app::AppManager rollback_manager;
    (void)rollback_manager.registerModule(ready);
    (void)rollback_manager.registerModule(failing);
    (void)rollback_manager.registerModule(never_started);
    const auto result = rollback_manager.initializeAll();
    check(result.status == gamebox::app::LifecycleStatus::module_failed,
          "initialization failure must be reported");
    const int rollback_expected[] = {4, 5, -5, -4};
    check(log_size == (sizeof(rollback_expected) / sizeof(rollback_expected[0])),
          "rollback must include failed and previously initialized modules");
    for (std::size_t index = 0U; index < log_size; ++index) {
        check(log[index] == rollback_expected[index],
              "failed initialization must roll back in reverse order");
    }

    log_size = 0U;
    FakeModule cleanup_first("cleanup-first", 7, log, log_size);
    FakeModule cleanup_failing("cleanup-failing", 8, log, log_size, true, false);
    FakeModule cleanup_last("cleanup-last", 9, log, log_size);
    gamebox::app::AppManager cleanup_manager;
    (void)cleanup_manager.registerModule(cleanup_first);
    (void)cleanup_manager.registerModule(cleanup_failing);
    (void)cleanup_manager.registerModule(cleanup_last);
    check(static_cast<bool>(cleanup_manager.initializeAll()),
          "cleanup scenario must initialize first");
    const auto cleanup_result = cleanup_manager.deinitializeAll();
    check(cleanup_result.status == gamebox::app::LifecycleStatus::module_failed &&
              cleanup_result.module_name == "cleanup-failing",
          "deinitialization must identify the first cleanup failure");
    const int cleanup_expected[] = {7, 8, 9, -9, -8, -7};
    check(log_size == (sizeof(cleanup_expected) / sizeof(cleanup_expected[0])),
          "cleanup failure must not prevent remaining reverse-order cleanup");
    for (std::size_t index = 0U; index < log_size; ++index) {
        check(log[index] == cleanup_expected[index],
              "cleanup manager must attempt every module after a failure");
    }
    check(cleanup_manager.hasCleanupFailure(),
          "manager must enter a faulted state after incomplete cleanup");
}

} // namespace

int main()
{
    testDebounceAndClick();
    testDoubleClick();
    testLongPressAndRepeat();
    testTickWrap();
    testTween();
    testSnakeModel();
    testDinoModel();
    testAirRaidModel();
    testTetrisModel();
    testPongModel();
    testGameMenu();
    testSettingsMenu();
    testCanvas();
    testSettingsCodec();
    testCalendarCodec();
    testModuleLifecycle();

    if (failures == 0) {
        std::cout << "All GameBox host tests passed\n";
        return 0;
    }
    std::cerr << failures << " test(s) failed\n";
    return 1;
}
