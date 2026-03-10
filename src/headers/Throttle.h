#pragma once

#include <chrono>

namespace TalosAP {

/// Lightweight wall-clock throttle for individual subsystems.
///
/// Call Ready() each frame; it returns true only after the configured
/// interval has elapsed since the last time it returned true.
///
/// Usage:
///   Throttle m_hudThrottle{200};          // 200 ms
///   if (m_hudThrottle.Ready()) DoWork();
class Throttle {
public:
    explicit Throttle(int intervalMs = 200) noexcept
        : m_intervalMs(intervalMs) {}

    /// Returns true if at least m_intervalMs milliseconds have passed
    /// since the last time this returned true.
    /// On first call the timer is initialised and the function returns true.
    bool Ready() noexcept {
        auto now = std::chrono::steady_clock::now();

        // First call — initialise and fire.
        if (m_last == TimePoint{}) {
            m_last = now;
            return true;
        }

        auto elapsed = std::chrono::duration_cast<
            std::chrono::milliseconds>(now - m_last).count();
        if (elapsed >= m_intervalMs) {
            m_last = now;
            return true;
        }
        return false;
    }

    /// Change the interval at runtime (e.g. from config).
    void SetInterval(int ms) noexcept { m_intervalMs = ms; }

    /// Current configured interval.
    int Interval() const noexcept { return m_intervalMs; }

private:
    using TimePoint = std::chrono::steady_clock::time_point;

    int       m_intervalMs;
    TimePoint m_last{};
};

} // namespace TalosAP
