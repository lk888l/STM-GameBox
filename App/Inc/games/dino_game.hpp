#pragma once

#include <cstddef>
#include <cstdint>

#include "games/game_types.hpp"

namespace gamebox::games {

class DinoGame final {
public:
    using State = GamePhase;

    static constexpr std::size_t kObstacleCount = 3U;
    static constexpr std::int16_t kDinoX = 12;
    static constexpr std::int16_t kGroundY = 58;

    struct Obstacle {
        std::int16_t x{0};
        std::uint8_t width{0U};
        std::uint8_t height{0U};
        bool active{false};
    };

    void reset(std::uint32_t seed);
    void start();
    void jump();
    void update(std::uint32_t now_ms);

    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] std::uint16_t score() const { return score_; }
    [[nodiscard]] std::uint8_t jumpHeight() const;
    [[nodiscard]] const Obstacle& obstacle(std::size_t index) const
    {
        return obstacles_[index];
    }

private:
    void step();
    void spawnObstacle();
    [[nodiscard]] bool collides(const Obstacle& obstacle) const;

    Random random_{};
    Obstacle obstacles_[kObstacleCount]{};
    State state_{State::ready};
    std::uint32_t next_step_ms_{0U};
    std::uint16_t spawn_steps_{16U};
    std::uint16_t score_{0U};
    std::uint8_t jump_frame_{0U};
};

} // namespace gamebox::games
