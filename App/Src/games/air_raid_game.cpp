#include "games/air_raid_game.hpp"

namespace gamebox::games {

namespace {

constexpr std::uint32_t kStepPeriodMs = 33U;
constexpr std::int16_t kPlayerX = 5;
constexpr std::int16_t kPlayerWidth = 12;
constexpr std::int16_t kPlayerHeight = 7;
constexpr std::int16_t kEnemyWidth = 9;
constexpr std::int16_t kEnemyHeight = 7;

bool overlaps(const std::int16_t left_a,
              const std::int16_t top_a,
              const std::int16_t width_a,
              const std::int16_t height_a,
              const std::int16_t left_b,
              const std::int16_t top_b,
              const std::int16_t width_b,
              const std::int16_t height_b)
{
    return left_a < left_b + width_b && left_a + width_a > left_b &&
           top_a < top_b + height_b && top_a + height_a > top_b;
}

} // namespace

void AirRaidGame::reset(const std::uint32_t seed)
{
    random_.reseed(seed);
    for (Entity& bullet : bullets_) {
        bullet = {};
    }
    for (Entity& enemy : enemies_) {
        enemy = {};
    }
    state_ = State::ready;
    next_step_ms_ = 0U;
    spawn_steps_ = 10U;
    score_ = 0U;
    player_y_ = 32;
    vertical_ = 0;
    lives_ = 3U;
}

void AirRaidGame::start()
{
    if (state_ == State::ready) {
        state_ = State::playing;
        next_step_ms_ = 0U;
    }
}

void AirRaidGame::setVertical(const std::int8_t direction)
{
    vertical_ = direction < 0 ? -1 : (direction > 0 ? 1 : 0);
}

bool AirRaidGame::fire()
{
    if (state_ != State::playing) {
        return false;
    }
    for (Entity& bullet : bullets_) {
        if (!bullet.active) {
            bullet = {static_cast<std::int16_t>(kPlayerX + kPlayerWidth),
                      static_cast<std::int16_t>(player_y_ + 3),
                      true};
            return true;
        }
    }
    return false;
}

void AirRaidGame::spawnEnemy()
{
    for (Entity& enemy : enemies_) {
        if (!enemy.active) {
            enemy = {127,
                     static_cast<std::int16_t>(13U + random_.bounded(43U)),
                     true};
            return;
        }
    }
}

bool AirRaidGame::intersects(const Entity& bullet, const Entity& enemy)
{
    return bullet.active && enemy.active &&
           overlaps(bullet.x, bullet.y, 3, 2,
                    enemy.x, enemy.y, kEnemyWidth, kEnemyHeight);
}

void AirRaidGame::loseLife()
{
    if (lives_ > 0U) {
        --lives_;
    }
    if (lives_ == 0U) {
        state_ = State::game_over;
    }
}

void AirRaidGame::step()
{
    player_y_ = static_cast<std::int16_t>(player_y_ + vertical_ * 2);
    if (player_y_ < 12) {
        player_y_ = 12;
    } else if (player_y_ > 56) {
        player_y_ = 56;
    }

    if (spawn_steps_ > 0U) {
        --spawn_steps_;
    }
    if (spawn_steps_ == 0U) {
        spawnEnemy();
        const std::uint32_t difficulty = score_ < 40U ? score_ / 8U : 5U;
        spawn_steps_ = static_cast<std::uint16_t>(14U + random_.bounded(11U) - difficulty);
    }

    for (Entity& bullet : bullets_) {
        if (bullet.active) {
            bullet.x = static_cast<std::int16_t>(bullet.x + 4);
            if (bullet.x >= 128) {
                bullet.active = false;
            }
        }
    }

    const std::int16_t enemy_speed = score_ < 20U ? 2 : 3;
    for (Entity& enemy : enemies_) {
        if (!enemy.active) {
            continue;
        }
        enemy.x = static_cast<std::int16_t>(enemy.x - enemy_speed);
        bool destroyed = false;
        for (Entity& bullet : bullets_) {
            if (intersects(bullet, enemy)) {
                bullet.active = false;
                enemy.active = false;
                destroyed = true;
                ++score_;
                break;
            }
        }
        if (destroyed) {
            continue;
        }
        if (overlaps(kPlayerX, player_y_, kPlayerWidth, kPlayerHeight,
                     enemy.x, enemy.y, kEnemyWidth, kEnemyHeight) ||
            enemy.x + kEnemyWidth < 0) {
            enemy.active = false;
            loseLife();
            if (state_ == State::game_over) {
                return;
            }
        }
    }
}

void AirRaidGame::update(const std::uint32_t now_ms)
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
