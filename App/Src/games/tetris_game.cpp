#include "games/tetris_game.hpp"

namespace gamebox::games {

namespace {

// Four 4x4 row-major masks per tetromino: I, O, T, S, Z, J, L.
constexpr std::uint16_t kMasks[TetrisGame::kPieceCount][4] = {
    {0x00F0U, 0x2222U, 0x00F0U, 0x2222U},
    {0x0066U, 0x0066U, 0x0066U, 0x0066U},
    {0x0072U, 0x0262U, 0x0270U, 0x0232U},
    {0x0036U, 0x0462U, 0x0036U, 0x0462U},
    {0x0063U, 0x0264U, 0x0063U, 0x0264U},
    {0x0071U, 0x0226U, 0x0470U, 0x0322U},
    {0x0074U, 0x0622U, 0x0170U, 0x0223U},
};

} // namespace

bool TetrisGame::pieceCell(const std::uint8_t piece,
                           const std::uint8_t rotation,
                           const std::uint8_t x,
                           const std::uint8_t y)
{
    if (piece >= kPieceCount || x >= 4U || y >= 4U) {
        return false;
    }
    const std::uint8_t bit = static_cast<std::uint8_t>(y * 4U + x);
    return (kMasks[piece][rotation & 3U] & (1U << bit)) != 0U;
}

void TetrisGame::reset(const std::uint32_t seed)
{
    random_.reseed(seed);
    for (std::uint16_t& row : rows_) {
        row = 0U;
    }
    state_ = State::ready;
    next_step_ms_ = 0U;
    score_ = 0U;
    lines_ = 0U;
    next_piece_ = static_cast<std::uint8_t>(random_.bounded(kPieceCount));
    spawnPiece();
}

void TetrisGame::start()
{
    if (state_ == State::ready) {
        state_ = State::playing;
        next_step_ms_ = 0U;
    }
}

bool TetrisGame::settled(const std::uint8_t x, const std::uint8_t y) const
{
    return x < kColumns && y < kRows && (rows_[y] & (1U << x)) != 0U;
}

bool TetrisGame::active(const std::uint8_t x, const std::uint8_t y) const
{
    for (std::uint8_t local_y = 0U; local_y < 4U; ++local_y) {
        for (std::uint8_t local_x = 0U; local_x < 4U; ++local_x) {
            if (!pieceCell(piece_, rotation_, local_x, local_y)) {
                continue;
            }
            const std::int16_t world_x = static_cast<std::int16_t>(piece_x_) + local_x;
            const std::int16_t world_y = static_cast<std::int16_t>(piece_y_) + local_y;
            if (world_x == x && world_y == y) {
                return true;
            }
        }
    }
    return false;
}

bool TetrisGame::fits(const std::int8_t x,
                      const std::int8_t y,
                      const std::uint8_t rotation) const
{
    for (std::uint8_t local_y = 0U; local_y < 4U; ++local_y) {
        for (std::uint8_t local_x = 0U; local_x < 4U; ++local_x) {
            if (!pieceCell(piece_, rotation, local_x, local_y)) {
                continue;
            }
            const std::int16_t world_x = static_cast<std::int16_t>(x) + local_x;
            const std::int16_t world_y = static_cast<std::int16_t>(y) + local_y;
            if (world_x < 0 || world_x >= kColumns || world_y >= kRows) {
                return false;
            }
            if (world_y >= 0 && settled(static_cast<std::uint8_t>(world_x),
                                        static_cast<std::uint8_t>(world_y))) {
                return false;
            }
        }
    }
    return true;
}

bool TetrisGame::move(const std::int8_t horizontal)
{
    if (state_ != State::playing || horizontal == 0) {
        return false;
    }
    const std::int8_t candidate = static_cast<std::int8_t>(piece_x_ + (horizontal < 0 ? -1 : 1));
    if (!fits(candidate, piece_y_, rotation_)) {
        return false;
    }
    piece_x_ = candidate;
    return true;
}

bool TetrisGame::rotate(const std::int8_t direction)
{
    if (state_ != State::playing || direction == 0) {
        return false;
    }
    const std::uint8_t candidate = static_cast<std::uint8_t>(
        (rotation_ + (direction > 0 ? 1U : 3U)) & 3U);
    constexpr std::int8_t kicks[] = {0, -1, 1, -2, 2};
    for (const std::int8_t kick : kicks) {
        const std::int8_t candidate_x = static_cast<std::int8_t>(piece_x_ + kick);
        if (fits(candidate_x, piece_y_, candidate)) {
            piece_x_ = candidate_x;
            rotation_ = candidate;
            return true;
        }
    }
    return false;
}

bool TetrisGame::stepDown()
{
    const std::int8_t candidate_y = static_cast<std::int8_t>(piece_y_ + 1);
    if (fits(piece_x_, candidate_y, rotation_)) {
        piece_y_ = candidate_y;
        return true;
    }
    lockPiece();
    return false;
}

bool TetrisGame::softDrop()
{
    if (state_ != State::playing) {
        return false;
    }
    const bool moved = stepDown();
    if (moved) {
        ++score_;
    }
    return moved;
}

void TetrisGame::hardDrop()
{
    if (state_ != State::playing) {
        return;
    }
    while (stepDown()) {
        score_ += 2U;
    }
}

void TetrisGame::lockPiece()
{
    for (std::uint8_t local_y = 0U; local_y < 4U; ++local_y) {
        for (std::uint8_t local_x = 0U; local_x < 4U; ++local_x) {
            if (!pieceCell(piece_, rotation_, local_x, local_y)) {
                continue;
            }
            const std::int16_t world_x = static_cast<std::int16_t>(piece_x_) + local_x;
            const std::int16_t world_y = static_cast<std::int16_t>(piece_y_) + local_y;
            if (world_y < 0) {
                state_ = State::game_over;
                return;
            }
            rows_[world_y] = static_cast<std::uint16_t>(
                rows_[world_y] | (1U << static_cast<std::uint8_t>(world_x)));
        }
    }
    clearLines();
    spawnPiece();
}

void TetrisGame::clearLines()
{
    constexpr std::uint16_t full = (1U << kColumns) - 1U;
    std::uint8_t cleared = 0U;
    for (std::int16_t row = static_cast<std::int16_t>(kRows - 1U); row >= 0;) {
        if (rows_[row] != full) {
            --row;
            continue;
        }
        ++cleared;
        for (std::int16_t shift = row; shift > 0; --shift) {
            rows_[shift] = rows_[shift - 1];
        }
        rows_[0] = 0U;
    }
    lines_ = static_cast<std::uint16_t>(lines_ + cleared);
    constexpr std::uint16_t points[] = {0U, 100U, 300U, 500U, 800U};
    score_ += points[cleared];
}

void TetrisGame::spawnPiece()
{
    piece_ = next_piece_;
    next_piece_ = static_cast<std::uint8_t>(random_.bounded(kPieceCount));
    rotation_ = 0U;
    piece_x_ = 3;
    piece_y_ = -1;
    if (!fits(piece_x_, piece_y_, rotation_)) {
        state_ = State::game_over;
    }
}

std::uint32_t TetrisGame::gravityPeriodMs() const
{
    const std::uint32_t level = lines_ / 8U;
    const std::uint32_t reduction = level < 9U ? level * 45U : 405U;
    return 500U - reduction;
}

void TetrisGame::update(const std::uint32_t now_ms)
{
    if (state_ != State::playing) {
        return;
    }
    if (next_step_ms_ == 0U) {
        next_step_ms_ = now_ms + gravityPeriodMs();
        return;
    }
    std::uint8_t catch_up = 0U;
    while (static_cast<std::int32_t>(now_ms - next_step_ms_) >= 0 && catch_up < 3U &&
           state_ == State::playing) {
        (void)stepDown();
        next_step_ms_ += gravityPeriodMs();
        ++catch_up;
    }
    if (catch_up == 3U && static_cast<std::int32_t>(now_ms - next_step_ms_) >= 0) {
        next_step_ms_ = now_ms + gravityPeriodMs();
    }
}

} // namespace gamebox::games
