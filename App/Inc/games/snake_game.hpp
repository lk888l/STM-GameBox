#pragma once

#include <cstddef>
#include <cstdint>

#include "games/game_types.hpp"

namespace gamebox::games {

class SnakeGame final {
public:
    static constexpr std::uint8_t kColumns = 30U;
    static constexpr std::uint8_t kRows = 12U;
    static constexpr std::size_t kMaximumLength = 96U;

    enum class Direction : std::uint8_t { up, down, left, right };
    using State = GamePhase;

    struct Point {
        std::uint8_t x{0U};
        std::uint8_t y{0U};
    };

    void reset(std::uint32_t seed);
    void start();
    void turn(Direction direction);
    void update(std::uint32_t now_ms);

    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] std::uint16_t score() const { return score_; }
    [[nodiscard]] std::size_t length() const { return length_; }
    [[nodiscard]] const Point& segment(std::size_t index) const { return body_[index]; }
    [[nodiscard]] const Point& food() const { return food_; }

private:
    [[nodiscard]] bool occupies(const Point& point, std::size_t limit) const;
    void placeFood();
    void step();
    [[nodiscard]] std::uint32_t stepPeriodMs() const;

    Point body_[kMaximumLength]{};
    Point food_{};
    std::size_t length_{3U};
    Direction direction_{Direction::right};
    Direction requested_direction_{Direction::right};
    State state_{State::ready};
    Random random_{};
    std::uint32_t next_step_ms_{0U};
    std::uint16_t score_{0U};
};

} // namespace gamebox::games
