#pragma once

#include <cstdint>

#include "games/game_types.hpp"

namespace gamebox::games {

class PongGame final {
public:
    using State = GamePhase;

    void reset(std::uint32_t seed);
    void start();
    void setLeftDirection(std::int8_t direction);
    void setRightDirection(std::int8_t direction);
    void update(std::uint32_t now_ms);

    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] std::int16_t ballX() const { return ball_x_; }
    [[nodiscard]] std::int16_t ballY() const { return ball_y_; }
    [[nodiscard]] std::int16_t leftY() const { return left_y_; }
    [[nodiscard]] std::int16_t rightY() const { return right_y_; }
    [[nodiscard]] std::uint8_t leftHalfLength() const { return left_half_length_; }
    [[nodiscard]] std::uint8_t rightHalfLength() const { return right_half_length_; }
    [[nodiscard]] std::uint8_t winner() const { return winner_; }

private:
    void step();
    void resetBall(std::int8_t horizontal_direction);
    void miss(bool left_side);

    Random random_{};
    State state_{State::ready};
    std::uint32_t next_step_ms_{0U};
    std::int16_t ball_x_{64};
    std::int16_t ball_y_{34};
    std::int8_t velocity_x_{1};
    std::int8_t velocity_y_{1};
    std::int16_t left_y_{34};
    std::int16_t right_y_{34};
    std::int8_t left_direction_{0};
    std::int8_t right_direction_{0};
    std::uint8_t left_half_length_{8U};
    std::uint8_t right_half_length_{8U};
    std::uint8_t winner_{0U};
};

} // namespace gamebox::games
