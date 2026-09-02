#pragma once

#include <cstddef>
#include <cstdint>

#include "games/game_types.hpp"

namespace gamebox::games {

class AirRaidGame final {
public:
    using State = GamePhase;
    static constexpr std::size_t kBulletCount = 3U;
    static constexpr std::size_t kEnemyCount = 3U;

    struct Entity {
        std::int16_t x{0};
        std::int16_t y{0};
        bool active{false};
    };

    void reset(std::uint32_t seed);
    void start();
    void setVertical(std::int8_t direction);
    [[nodiscard]] bool fire();
    void update(std::uint32_t now_ms);

    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] std::uint16_t score() const { return score_; }
    [[nodiscard]] std::uint8_t lives() const { return lives_; }
    [[nodiscard]] std::int16_t playerY() const { return player_y_; }
    [[nodiscard]] const Entity& bullet(std::size_t index) const { return bullets_[index]; }
    [[nodiscard]] const Entity& enemy(std::size_t index) const { return enemies_[index]; }

private:
    void step();
    void spawnEnemy();
    void loseLife();
    [[nodiscard]] static bool intersects(const Entity& bullet, const Entity& enemy);

    Random random_{};
    Entity bullets_[kBulletCount]{};
    Entity enemies_[kEnemyCount]{};
    State state_{State::ready};
    std::uint32_t next_step_ms_{0U};
    std::uint16_t spawn_steps_{10U};
    std::uint16_t score_{0U};
    std::int16_t player_y_{32};
    std::int8_t vertical_{0};
    std::uint8_t lives_{3U};
};

} // namespace gamebox::games
