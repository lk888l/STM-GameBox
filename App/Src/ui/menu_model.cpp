#include "ui/menu_model.hpp"

namespace gamebox::ui {

namespace {

constexpr MenuEntry kHomeEntries[] = {
    {"GAMES", "6 classics", Icon::gamepad, Action::open, View::games},
    {"TOOLS", "Time & test", Icon::tools, Action::open, View::tools},
    {"CLOCK", "RTC dashboard", Icon::clock, Action::open, View::clock},
    {"SETTINGS", "Tune the box", Icon::settings, Action::open, View::settings},
};

constexpr MenuEntry kGameEntries[] = {
    {"DINO", "Jump & survive", Icon::dino, Action::open, View::dino},
    {"SNAKE", "Eat and grow", Icon::snake, Action::open, View::snake},
    {"AIR RAID", "Dodge and fire", Icon::plane, Action::open, View::air_raid},
    {"TETRIS", "Clear the lines", Icon::tetris, Action::open, View::tetris},
    {"PONG 2P", "Two players", Icon::pong, Action::open, View::pong},
    {"PIANO", "Eight-key synth", Icon::piano, Action::open, View::piano},
};

constexpr MenuEntry kToolEntries[] = {
    {"STOPWATCH", "Monotonic timer", Icon::stopwatch, Action::open, View::stopwatch},
    {"COUNTDOWN", "Adjustable timer", Icon::timer, Action::open, View::timer},
    {"INPUT LAB", "Button events", Icon::buttons, Action::open, View::input_lab},
    {"SYSTEM", "Runtime health", Icon::chip, Action::open, View::system},
};

constexpr MenuEntry kSettingEntries[] = {
    {"SOUND", "Buzzer feedback", Icon::speaker, Action::toggle_sound, View::settings},
    {"MOTION", "Animation level", Icon::motion, Action::cycle_motion, View::settings},
    {"BRIGHTNESS", "OLED contrast", Icon::brightness, Action::cycle_brightness, View::settings},
    {"HOME HEADER", "Time date pet title", Icon::clock, Action::cycle_home_header, View::settings},
    {"ABOUT", "Firmware details", Icon::info, Action::open, View::about},
};

template <std::size_t Size>
constexpr std::uint8_t countOf(const MenuEntry (&)[Size])
{
    static_assert(Size <= 255U);
    return static_cast<std::uint8_t>(Size);
}

constexpr MenuDefinition kHome{"GAMEBOX", kHomeEntries, countOf(kHomeEntries)};
constexpr MenuDefinition kGames{"GAMES", kGameEntries, countOf(kGameEntries)};
constexpr MenuDefinition kTools{"TOOLS", kToolEntries, countOf(kToolEntries)};
constexpr MenuDefinition kSettings{"SETTINGS", kSettingEntries, countOf(kSettingEntries)};

} // namespace

const MenuDefinition* menuFor(const View view)
{
    switch (view) {
    case View::home: return &kHome;
    case View::games: return &kGames;
    case View::tools: return &kTools;
    case View::settings: return &kSettings;
    default: return nullptr;
    }
}

const MenuEntry& entryAt(const View view, const std::uint8_t index)
{
    static constexpr MenuEntry fallback{
        "INVALID", "No menu entry", Icon::info, Action::unavailable, View::home};
    const MenuDefinition* const definition = menuFor(view);
    if (definition == nullptr || definition->count == 0U) {
        return fallback;
    }
    return definition->entries[index < definition->count ? index : 0U];
}

bool isMenu(const View view)
{
    return menuFor(view) != nullptr;
}

bool isGame(const View view)
{
    switch (view) {
    case View::snake:
    case View::dino:
    case View::air_raid:
    case View::tetris:
    case View::pong:
    case View::piano:
        return true;
    default:
        return false;
    }
}

} // namespace gamebox::ui
