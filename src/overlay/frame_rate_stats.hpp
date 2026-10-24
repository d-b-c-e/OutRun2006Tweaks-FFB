#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>

struct FrameRateSummary
{
    double averageFps = 0;
    double onePercentLowFps = 0;
    double worstMs = 0;
    bool ready = false;
};

// Fixed storage, one sort per ten-second window and no allocation per frame.
class FrameRateStats
{
    static constexpr std::size_t Capacity = 16384;
    std::array<float, Capacity> frames{};
    std::size_t count = 0;
    double seconds = 0;
    double smoothedMs = 0;
    FrameRateSummary summary{};

public:
    const FrameRateSummary& Summary() const { return summary; }
    double CurrentFps() const { return smoothedMs > 0 ? 1000.0 / smoothedMs : 0; }

    bool Add(double frameSeconds)
    {
        if (!std::isfinite(frameSeconds) || frameSeconds <= 0 || frameSeconds > 2)
        {
            count = 0;
            seconds = 0;
            smoothedMs = 0;
            summary.ready = false;
            return false;
        }
        const double ms = frameSeconds * 1000.0;
        smoothedMs = smoothedMs == 0 ? ms : smoothedMs * 0.9 + ms * 0.1;
        frames[count++] = static_cast<float>(ms);
        seconds += frameSeconds;
        if (seconds < 10 && count < Capacity) return false;

        std::sort(frames.begin(), frames.begin() + count, std::greater<float>());
        const std::size_t slowCount = std::max<std::size_t>(1, (count + 99) / 100);
        double slowSum = 0;
        for (std::size_t i = 0; i < slowCount; ++i) slowSum += frames[i];
        summary = { count / seconds, 1000.0 * slowCount / slowSum, frames[0], true };
        count = 0;
        seconds = 0;
        return true;
    }
};
