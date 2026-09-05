#pragma once

#include <cstdint>
#include <limits>

namespace gamebox::ui {

class Tween final {
public:
    static constexpr std::uint32_t kMaximumDurationMs = 60'000U;

    void jumpTo(const std::int32_t value)
    {
        from_ = value;
        to_ = value;
        started_ms_ = 0U;
        duration_ms_ = 0U;
    }

    void start(const std::int32_t from,
               const std::int32_t to,
               const std::uint32_t now_ms,
               const std::uint32_t duration_ms)
    {
        from_ = from;
        to_ = to;
        started_ms_ = now_ms;
        duration_ms_ = duration_ms > kMaximumDurationMs ? kMaximumDurationMs : duration_ms;
        if (duration_ms_ == 0U) {
            from_ = to;
        }
    }

    void retarget(const std::int32_t target,
                  const std::uint32_t now_ms,
                  const std::uint32_t duration_ms)
    {
        start(value(now_ms), target, now_ms, duration_ms);
    }

    [[nodiscard]] bool active(const std::uint32_t now_ms) const
    {
        return duration_ms_ != 0U &&
               static_cast<std::uint32_t>(now_ms - started_ms_) < duration_ms_;
    }

    [[nodiscard]] std::int32_t value(const std::uint32_t now_ms) const
    {
        if (duration_ms_ == 0U) {
            return to_;
        }
        const std::uint32_t elapsed = now_ms - started_ms_;
        if (elapsed >= duration_ms_) {
            return to_;
        }

        // Q15 ease-out cubic: 1 - (1 - t)^3. It is deterministic and uses no FPU.
        constexpr std::uint32_t one = 32768U;
        // The duration cap guarantees elapsed * one fits in uint32_t.
        const std::uint32_t t = (elapsed * one) / duration_ms_;
        const std::uint32_t inverse = one - t;
        const std::uint32_t inverse_square = (inverse * inverse) >> 15U;
        const std::uint32_t inverse_cube = (inverse_square * inverse) >> 15U;
        const std::uint32_t eased = one - inverse_cube;
        const std::int64_t delta =
            static_cast<std::int64_t>(to_) - static_cast<std::int64_t>(from_);
        const std::int64_t scaled = delta * static_cast<std::int64_t>(eased);
        const std::int64_t result = static_cast<std::int64_t>(from_) +
                                    scaled / static_cast<std::int64_t>(one);
        return static_cast<std::int32_t>(result);
    }

private:
    std::int32_t from_{0};
    std::int32_t to_{0};
    std::uint32_t started_ms_{0U};
    std::uint32_t duration_ms_{0U};
};

enum class SpringSpeed : std::uint8_t {
    off,
    slow,
    fast,
};

/** Tracks fixed simulation steps independently from the OLED frame rate. */
class MotionClock final {
public:
    static constexpr std::uint32_t kStepIntervalMs = 8U;
    static constexpr std::uint32_t kMaximumCatchupSteps = 8U;

    void reset(const std::uint32_t now_ms) { stepped_at_ms_ = now_ms; }

    [[nodiscard]] std::uint32_t advance(const std::uint32_t now_ms)
    {
        const std::uint32_t due = (now_ms - stepped_at_ms_) / kStepIntervalMs;
        const std::uint32_t steps = due < kMaximumCatchupSteps ? due : kMaximumCatchupSteps;
        // Preserve fractional steps during normal rendering, but drop excessive
        // backlog after a stalled bus or debugger halt so work stays bounded.
        stepped_at_ms_ = due > kMaximumCatchupSteps
                             ? now_ms
                             : stepped_at_ms_ + steps * kStepIntervalMs;
        return steps;
    }

private:
    std::uint32_t stepped_at_ms_{0U};
};

/**
 * Deterministic one-dimensional damped spring in signed Q24.8 format.
 *
 * This is the C++ counterpart of the motion primitive used by the Embassy
 * firmware. MotionClock schedules one fixed step per 8 ms of elapsed time,
 * preserving animation speed across SPI/I2C frame rates without floating point.
 */
class Spring final {
public:
    constexpr explicit Spring(const std::int16_t value = 0)
        : position_(static_cast<std::int32_t>(value) * kOne),
          target_(position_)
    {
    }

    void setTarget(const std::int16_t value)
    {
        target_ = static_cast<std::int32_t>(value) * kOne;
    }

    void snapTo(const std::int16_t value)
    {
        const std::int32_t fixed = static_cast<std::int32_t>(value) * kOne;
        position_ = fixed;
        velocity_ = 0;
        target_ = fixed;
    }

    void step(const SpringSpeed speed)
    {
        std::int64_t stiffness = 0;
        std::int64_t damping = 0;
        switch (speed) {
        case SpringSpeed::off:
            position_ = target_;
            velocity_ = 0;
            return;
        case SpringSpeed::slow:
            stiffness = 34;
            damping = 206;
            break;
        case SpringSpeed::fast:
            stiffness = 62;
            damping = 184;
            break;
        }

        const std::int64_t displacement =
            static_cast<std::int64_t>(target_) - static_cast<std::int64_t>(position_);
        const std::int64_t acceleration = displacement * stiffness / kOne;
        const std::int64_t velocity =
            (static_cast<std::int64_t>(velocity_) + acceleration) * damping / kOne;
        velocity_ = clampToInt32(velocity);
        position_ = clampToInt32(static_cast<std::int64_t>(position_) + velocity_);

        if (absolute(static_cast<std::int64_t>(target_) - position_) <= 8 &&
            absolute(velocity_) <= 8) {
            position_ = target_;
            velocity_ = 0;
        }
    }

    [[nodiscard]] std::int16_t value() const
    {
        return static_cast<std::int16_t>((position_ + kOne / 2) / kOne);
    }

    [[nodiscard]] bool settled() const
    {
        return position_ == target_ && velocity_ == 0;
    }

private:
    static constexpr std::int32_t kOne = 1 << 8;

    [[nodiscard]] static constexpr std::int32_t clampToInt32(const std::int64_t value)
    {
        return value > std::numeric_limits<std::int32_t>::max()
                   ? std::numeric_limits<std::int32_t>::max()
               : value < std::numeric_limits<std::int32_t>::min()
                   ? std::numeric_limits<std::int32_t>::min()
                   : static_cast<std::int32_t>(value);
    }

    [[nodiscard]] static constexpr std::int64_t absolute(const std::int64_t value)
    {
        return value < 0 ? -value : value;
    }

    std::int32_t position_{0};
    std::int32_t velocity_{0};
    std::int32_t target_{0};
};

} // namespace gamebox::ui
