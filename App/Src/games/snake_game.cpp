#include "games/snake_game.hpp"

namespace gamebox::games {

namespace {

bool isOpposite(const SnakeGame::Direction left, const SnakeGame::Direction right)
{
    using Direction = SnakeGame::Direction;
    return (left == Direction::up && right == Direction::down) ||
           (left == Direction::down && right == Direction::up) ||
           (left == Direction::left && right == Direction::right) ||
           (left == Direction::right && right == Direction::left);
}

} // namespace

void SnakeGame::reset(const std::uint32_t seed)
{
    random_.reseed(seed);
    length_ = 3U;
    const std::uint8_t center_x = static_cast<std::uint8_t>(kColumns / 2U);
    const std::uint8_t center_y = static_cast<std::uint8_t>(kRows / 2U);
    body_[0] = {center_x, center_y};
    body_[1] = {static_cast<std::uint8_t>(center_x - 1U), center_y};
    body_[2] = {static_cast<std::uint8_t>(center_x - 2U), center_y};
    direction_ = Direction::right;
    requested_direction_ = direction_;
    state_ = State::ready;
    next_step_ms_ = 0U;
    score_ = 0U;
    placeFood();
}

void SnakeGame::start()
{
    if (state_ == State::ready) {
        state_ = State::playing;
        next_step_ms_ = 0U;
    }
}

void SnakeGame::turn(const Direction direction)
{
    if (!isOpposite(direction, direction_)) {
        requested_direction_ = direction;
    }
}

bool SnakeGame::occupies(const Point& point, const std::size_t limit) const
{
    for (std::size_t index = 0U; index < limit; ++index) {
        if (body_[index].x == point.x && body_[index].y == point.y) {
            return true;
        }
    }
    return false;
}

void SnakeGame::placeFood()
{
    constexpr std::size_t cell_count =
        static_cast<std::size_t>(kColumns) * static_cast<std::size_t>(kRows);
    std::size_t candidate = static_cast<std::size_t>(random_.bounded(cell_count));
    for (std::size_t attempt = 0U; attempt < cell_count; ++attempt) {
        const Point point{
            static_cast<std::uint8_t>(candidate % kColumns),
            static_cast<std::uint8_t>(candidate / kColumns),
        };
        if (!occupies(point, length_)) {
            food_ = point;
            return;
        }
        candidate = (candidate + 1U) % cell_count;
    }
}

std::uint32_t SnakeGame::stepPeriodMs() const
{
    const std::uint32_t reduction = static_cast<std::uint32_t>(score_) * 3U;
    return reduction < 80U ? 150U - reduction : 70U;
}

void SnakeGame::update(const std::uint32_t now_ms)
{
    if (state_ != State::playing) {
        return;
    }
    if (next_step_ms_ == 0U) {
        next_step_ms_ = now_ms + stepPeriodMs();
        return;
    }
    std::uint8_t catch_up = 0U;
    while (static_cast<std::int32_t>(now_ms - next_step_ms_) >= 0 && catch_up < 4U &&
           state_ == State::playing) {
        step();
        next_step_ms_ += stepPeriodMs();
        ++catch_up;
    }
    if (catch_up == 4U && static_cast<std::int32_t>(now_ms - next_step_ms_) >= 0) {
        next_step_ms_ = now_ms + stepPeriodMs();
    }
}

void SnakeGame::step()
{
    direction_ = requested_direction_;
    Point next = body_[0];
    switch (direction_) {
    case Direction::up:
        if (next.y == 0U) { state_ = State::game_over; return; }
        --next.y;
        break;
    case Direction::down:
        if (next.y + 1U >= kRows) { state_ = State::game_over; return; }
        ++next.y;
        break;
    case Direction::left:
        if (next.x == 0U) { state_ = State::game_over; return; }
        --next.x;
        break;
    case Direction::right:
        if (next.x + 1U >= kColumns) { state_ = State::game_over; return; }
        ++next.x;
        break;
    }

    const bool ate = next.x == food_.x && next.y == food_.y;
    const std::size_t collision_limit = ate ? length_ : length_ - 1U;
    if (occupies(next, collision_limit)) {
        state_ = State::game_over;
        return;
    }

    const std::size_t next_length =
        ate && length_ < kMaximumLength ? length_ + 1U : length_;
    for (std::size_t index = next_length - 1U; index > 0U; --index) {
        body_[index] = body_[index - 1U];
    }
    body_[0] = next;
    length_ = next_length;

    if (ate) {
        ++score_;
        placeFood();
    }
}

} // namespace gamebox::games
