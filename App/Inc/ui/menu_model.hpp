#pragma once

#include <cstddef>
#include <cstdint>

namespace gamebox::ui {

enum class View : std::uint8_t {
    home,
    games,
    tools,
    clock,
    settings,
    snake,
    dino,
    air_raid,
    tetris,
    pong,
    piano,
    stopwatch,
    timer,
    input_lab,
    system,
    about,
    count,
};

enum class Icon : std::uint8_t {
    gamepad,
    tools,
    clock,
    settings,
    snake,
    dino,
    plane,
    tetris,
    pong,
    piano,
    stopwatch,
    timer,
    buttons,
    chip,
    info,
    speaker,
    motion,
    brightness,
};

enum class Action : std::uint8_t {
    open,
    toggle_sound,
    cycle_motion,
    cycle_brightness,
    unavailable,
};

struct MenuEntry {
    const char* label;
    const char* subtitle;
    Icon icon;
    Action action;
    View target;
};

struct MenuDefinition {
    const char* title;
    const MenuEntry* entries;
    std::uint8_t count;
};

[[nodiscard]] const MenuDefinition* menuFor(View view);
[[nodiscard]] const MenuEntry& entryAt(View view, std::uint8_t index);
[[nodiscard]] bool isMenu(View view);
[[nodiscard]] bool isGame(View view);

} // namespace gamebox::ui
