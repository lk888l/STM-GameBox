#pragma once

#include <cstdint>

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

} // namespace gamebox::ui
