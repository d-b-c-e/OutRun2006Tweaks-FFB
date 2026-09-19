#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace WheelInput
{
struct Calibration
{
    bool enabled = false;
    float minimum = 0, center = 32767.5f, maximum = 65535;
};

inline bool Valid(const Calibration& c, bool steering)
{
    return std::isfinite(c.minimum) && std::isfinite(c.center) && std::isfinite(c.maximum) &&
        c.minimum >= 0 && c.maximum <= 65535 && c.maximum - c.minimum >= 8192 &&
        c.center >= c.minimum && c.center <= c.maximum &&
        (!steering || (c.center - c.minimum >= 512 && c.maximum - c.center >= 512));
}

inline float Normalize(float raw, const Calibration& c, bool steering, bool invert, float deadzone)
{
    if (!Valid(c, steering) || !std::isfinite(raw) || !std::isfinite(deadzone) || deadzone < 0 || deadzone >= 1)
        return 0;
    float value = steering ? (raw >= c.center ? (raw - c.center) / (c.maximum - c.center)
        : (raw - c.center) / (c.center - c.minimum)) : (raw - c.minimum) / (c.maximum - c.minimum);
    value = std::clamp(value, steering ? -1.0f : 0.0f, 1.0f);
    if (invert) value = steering ? -value : 1 - value;
    if (steering)
    {
        const float magnitude = (std::max)(0.0f, std::abs(value) - deadzone) / (1 - deadzone);
        return std::copysign(magnitude, value);
    }
    return (std::max)(0.0f, value - deadzone) / (1 - deadzone);
}

// Detection accumulates travel across the capture, so moving two controls does
// not silently bind whichever moved last. A new rest capture clears ambiguity.
struct Travel
{
    std::array<float, 8> rest{}, low{}, high{};
    void Begin(const std::array<long, 8>& values)
    {
        for (int i = 0; i < 8; ++i) rest[i] = low[i] = high[i] = static_cast<float>(values[i]);
    }
    void Observe(const std::array<long, 8>& values)
    {
        for (int i = 0; i < 8; ++i)
        {
            low[i] = (std::min)(low[i], static_cast<float>(values[i]));
            high[i] = (std::max)(high[i], static_cast<float>(values[i]));
        }
    }
    int Candidate() const
    {
        int found = -1;
        for (int i = 0; i < 8; ++i)
            if (high[i] - low[i] >= 8192)
            {
                if (found >= 0) return -2;
                found = i;
            }
        return found;
    }
    Calibration Endpoints(int axis, bool steering) const
    {
        return {true, low[axis], steering ? rest[axis] : (low[axis] + high[axis]) / 2, high[axis]};
    }
};
}
