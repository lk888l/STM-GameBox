#include "games/dino_game.hpp"

namespace gamebox::games {

namespace {

constexpr std::uint8_t kJumpArc[] = {
    0U, 5U, 10U, 14U, 18U, 21U, 23U, 24U, 23U, 21U, 18U, 14U, 10U, 5U, 0U,
};
constexpr std::uint32_t kStepPeriodMs = 45U;
constexpr std::uint8_t kDinoWidth = 10U;
constexpr std::uint8_t kDinoHeight = 12U;

} // namespace

void DinoGame::reset(const std::uint32_t seed)
{
    random_.reseed(seed);
    for (Obstacle& obstacle : obstacles_) {
        obstacle = {};
    }
    state_ = State::ready;
    next_step_ms_ = 0U;
    spawn_steps_ = 18U;
    score_ = 0U;
    jump_frame_ = 0U;
}

void DinoGame::start()
{
    if (state_ == State::ready) {
        state_ = State::playing;
        next_step_ms_ = 0U;
    }
}

void DinoGame::jump()
{
    if (state_ == State::playing && jump_frame_ == 0U) {
        jump_frame_ = 1U;
    }
}

std::uint8_t DinoGame::jumpHeight() const
{
    return kJumpArc[jump_frame_];
}

void DinoGame::spawnObstacle()
{
    for (Obstacle& obstacle : obstacles_) {
        if (!obstacle.active) {
            const bool tall = random_.bounded(3U) == 0U;
            obstacle.x = 127;
            obstacle.width = tall ? 6U : 9U;
            obstacle.height = tall ? 15U : 9U;
            obstacle.active = true;
            return;
        }
    }
}

bool DinoGame::collides(const Obstacle& obstacle) const
{
    if (!obstacle.active) {
        return false;
    }
    const std::int16_t dino_left = kDinoX;
    const std::int16_t dino_right = static_cast<std::int16_t>(kDinoX + kDinoWidth - 1U);
    const std::int16_t dino_bottom = static_cast<std::int16_t>(kGroundY - jumpHeight());
    const std::int16_t dino_top = static_cast<std::int16_t>(
        dino_bottom - static_cast<std::int16_t>(kDinoHeight) + 1);
    const std::int16_t obstacle_right =
        static_cast<std::int16_t>(obstacle.x + static_cast<std::int16_t>(obstacle.width) - 1);
    const std::int16_t obstacle_top =
        static_cast<std::int16_t>(kGroundY - obstacle.height + 1U);
    return dino_right >= obstacle.x && dino_left <= obstacle_right &&
           dino_bottom >= obstacle_top && dino_top <= kGroundY;
}

void DinoGame::step()
{
    if (jump_frame_ != 0U) {
        ++jump_frame_;
        if (jump_frame_ >= (sizeof(kJumpArc) / sizeof(kJumpArc[0]))) {
            jump_frame_ = 0U;
        }
    }

    if (spawn_steps_ > 0U) {
        --spawn_steps_;
    }
    if (spawn_steps_ == 0U) {
        spawnObstacle();
        const std::uint32_t difficulty = score_ < 60U ? score_ / 10U : 6U;
        spawn_steps_ = static_cast<std::uint16_t>(
            19U + random_.bounded(13U) - difficulty);
    }

    const std::int16_t speed = score_ < 25U ? 3 : (score_ < 70U ? 4 : 5);
    for (Obstacle& obstacle : obstacles_) {
        if (!obstacle.active) {
            continue;
        }
        obstacle.x = static_cast<std::int16_t>(obstacle.x - speed);
        if (collides(obstacle)) {
            state_ = State::game_over;
            return;
        }
        if (static_cast<std::int16_t>(obstacle.x + obstacle.width) < 0) {
            obstacle.active = false;
            ++score_;
        }
    }
}

void DinoGame::update(const std::uint32_t now_ms)
{
    if (state_ != State::playing) {
        return;
    }
    if (next_step_ms_ == 0U) {
        next_step_ms_ = now_ms + kStepPeriodMs;
        return;
    }

    std::uint8_t catch_up = 0U;
    while (static_cast<std::int32_t>(now_ms - next_step_ms_) >= 0 && catch_up < 5U &&
           state_ == State::playing) {
        step();
        next_step_ms_ += kStepPeriodMs;
        ++catch_up;
    }
    if (catch_up == 5U && static_cast<std::int32_t>(now_ms - next_step_ms_) >= 0) {
        next_step_ms_ = now_ms + kStepPeriodMs;
    }
}

} // namespace gamebox::games
