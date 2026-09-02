#pragma once

#include <cstdint>

#include "games/game_types.hpp"

namespace gamebox::games {

class TetrisGame final {
public:
    using State = GamePhase;
    static constexpr std::uint8_t kColumns = 10U;
    static constexpr std::uint8_t kRows = 15U;
    static constexpr std::uint8_t kPieceCount = 7U;

    void reset(std::uint32_t seed);
    void start();
    [[nodiscard]] bool move(std::int8_t horizontal);
    [[nodiscard]] bool rotate(std::int8_t direction);
    [[nodiscard]] bool softDrop();
    void hardDrop();
    void update(std::uint32_t now_ms);

    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] std::uint32_t score() const { return score_; }
    [[nodiscard]] std::uint16_t lines() const { return lines_; }
    [[nodiscard]] std::uint8_t nextPiece() const { return next_piece_; }
    [[nodiscard]] bool settled(std::uint8_t x, std::uint8_t y) const;
    [[nodiscard]] bool active(std::uint8_t x, std::uint8_t y) const;
    [[nodiscard]] static bool pieceCell(std::uint8_t piece,
                                        std::uint8_t rotation,
                                        std::uint8_t x,
                                        std::uint8_t y);

private:
    [[nodiscard]] bool fits(std::int8_t x,
                            std::int8_t y,
                            std::uint8_t rotation) const;
    [[nodiscard]] bool stepDown();
    void lockPiece();
    void clearLines();
    void spawnPiece();
    [[nodiscard]] std::uint32_t gravityPeriodMs() const;

    Random random_{};
    std::uint16_t rows_[kRows]{};
    State state_{State::ready};
    std::uint32_t next_step_ms_{0U};
    std::uint32_t score_{0U};
    std::uint16_t lines_{0U};
    std::int8_t piece_x_{3};
    std::int8_t piece_y_{-1};
    std::uint8_t piece_{0U};
    std::uint8_t next_piece_{0U};
    std::uint8_t rotation_{0U};
};

} // namespace gamebox::games
