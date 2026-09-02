#include "games/pong_game.hpp"

namespace gamebox::games {

namespace {

constexpr std::uint32_t kStepPeriodMs = 22U;
constexpr std::int16_t kTop = 11;
constexpr std::int16_t kBottom = 62;
constexpr std::int16_t kLeftPaddleX = 7;
constexpr std::int16_t kRightPaddleX = 120;

std::int8_t normalized(const std::int8_t value)
{
    return value < 0 ? -1 : (value > 0 ? 1 : 0);
}

} // namespace

void PongGame::reset(const std::uint32_t seed)
{
    random_.reseed(seed);
    state_ = State::ready;
    next_step_ms_ = 0U;
    left_y_ = 36;
    right_y_ = 36;
    left_direction_ = 0;
    right_direction_ = 0;
    left_half_length_ = 8U;
    right_half_length_ = 8U;
    winner_ = 0U;
    resetBall(random_.bounded(2U) == 0U ? -1 : 1);
}

void PongGame::start()
{
    if (state_ == State::ready) {
        state_ = State::playing;
        next_step_ms_ = 0U;
    }
}

void PongGame::setLeftDirection(const std::int8_t direction)
{
    left_direction_ = normalized(direction);
}

void PongGame::setRightDirection(const std::int8_t direction)
{
    right_direction_ = normalized(direction);
}

void PongGame::resetBall(const std::int8_t horizontal_direction)
{
    ball_x_ = 64;
    ball_y_ = static_cast<std::int16_t>(23U + random_.bounded(24U));
    velocity_x_ = horizontal_direction < 0 ? -1 : 1;
    velocity_y_ = random_.bounded(2U) == 0U ? -1 : 1;
}

void PongGame::miss(const bool left_side)
{
    std::uint8_t& half_length = left_side ? left_half_length_ : right_half_length_;
    if (half_length > 2U) {
        --half_length;
        resetBall(left_side ? 1 : -1);
        return;
    }
    winner_ = left_side ? 2U : 1U;
    state_ = State::game_over;
}

void PongGame::step()
{
    left_y_ = static_cast<std::int16_t>(left_y_ + left_direction_);
    right_y_ = static_cast<std::int16_t>(right_y_ + right_direction_);
    const std::int16_t left_limit = static_cast<std::int16_t>(left_half_length_ + kTop);
    const std::int16_t right_limit = static_cast<std::int16_t>(right_half_length_ + kTop);
    if (left_y_ < left_limit) { left_y_ = left_limit; }
    if (right_y_ < right_limit) { right_y_ = right_limit; }
    const std::int16_t left_bottom = static_cast<std::int16_t>(kBottom - left_half_length_);
    const std::int16_t right_bottom = static_cast<std::int16_t>(kBottom - right_half_length_);
    if (left_y_ > left_bottom) { left_y_ = left_bottom; }
    if (right_y_ > right_bottom) { right_y_ = right_bottom; }

    ball_x_ = static_cast<std::int16_t>(ball_x_ + velocity_x_);
    ball_y_ = static_cast<std::int16_t>(ball_y_ + velocity_y_);
    if (ball_y_ <= kTop + 1) {
        ball_y_ = kTop + 1;
        velocity_y_ = 1;
    } else if (ball_y_ >= kBottom - 1) {
        ball_y_ = kBottom - 1;
        velocity_y_ = -1;
    }

    if (velocity_x_ < 0 && ball_x_ <= kLeftPaddleX + 1) {
        const std::int16_t distance = ball_y_ > left_y_ ?
                                          static_cast<std::int16_t>(ball_y_ - left_y_) :
                                          static_cast<std::int16_t>(left_y_ - ball_y_);
        if (ball_x_ >= kLeftPaddleX - 1 && distance <= left_half_length_) {
            ball_x_ = kLeftPaddleX + 2;
            velocity_x_ = 1;
            if (ball_y_ < left_y_) { velocity_y_ = -1; }
            if (ball_y_ > left_y_) { velocity_y_ = 1; }
        } else if (ball_x_ < 0) {
            miss(true);
        }
    } else if (velocity_x_ > 0 && ball_x_ >= kRightPaddleX - 1) {
        const std::int16_t distance = ball_y_ > right_y_ ?
                                          static_cast<std::int16_t>(ball_y_ - right_y_) :
                                          static_cast<std::int16_t>(right_y_ - ball_y_);
        if (ball_x_ <= kRightPaddleX + 1 && distance <= right_half_length_) {
            ball_x_ = kRightPaddleX - 2;
            velocity_x_ = -1;
            if (ball_y_ < right_y_) { velocity_y_ = -1; }
            if (ball_y_ > right_y_) { velocity_y_ = 1; }
        } else if (ball_x_ > 127) {
            miss(false);
        }
    }
}

void PongGame::update(const std::uint32_t now_ms)
{
    if (state_ != State::playing) {
        return;
    }
    if (next_step_ms_ == 0U) {
        next_step_ms_ = now_ms + kStepPeriodMs;
        return;
    }
    std::uint8_t catch_up = 0U;
    while (static_cast<std::int32_t>(now_ms - next_step_ms_) >= 0 && catch_up < 6U &&
           state_ == State::playing) {
        step();
        next_step_ms_ += kStepPeriodMs;
        ++catch_up;
    }
    if (catch_up == 6U && static_cast<std::int32_t>(now_ms - next_step_ms_) >= 0) {
        next_step_ms_ = now_ms + kStepPeriodMs;
    }
}

} // namespace gamebox::games
