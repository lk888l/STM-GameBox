#pragma once

#include <cstdint>

namespace gamebox::games {

enum class GamePhase : std::uint8_t {
    ready,
    playing,
    game_over,
};

class Random final {
public:
    explicit Random(const std::uint32_t seed = 0xA341316CU) { reseed(seed); }

    void reseed(const std::uint32_t seed)
    {
        state_ = seed != 0U ? seed : 0xA341316CU;
    }

    [[nodiscard]] std::uint32_t next()
    {
        std::uint32_t value = state_;
        value ^= value << 13U;
        value ^= value >> 17U;
        value ^= value << 5U;
        state_ = value;
        return value;
    }

    [[nodiscard]] std::uint32_t bounded(const std::uint32_t upper_bound)
    {
        return upper_bound == 0U ? 0U : next() % upper_bound;
    }

private:
    std::uint32_t state_{0xA341316CU};
};

} // namespace gamebox::games
