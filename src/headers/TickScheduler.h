#pragma once

#include <chrono>
#include <cstdint>

namespace TalosAP {

/// Wall-clock-based tick gate.
///
/// Decouples the main update loop from frame rate.  Call Advance()
/// every frame; ShouldTick() returns true only once TICK_INTERVAL_MS
/// of real wall-clock time has elapsed.
///
/// Individual subsystems are responsible for their own pacing if
/// they need to run slower than the main tick rate.
///
/// Usage:
///   m_scheduler.Advance();
///   if (!m_scheduler.ShouldTick()) return;  // gate the loop
///   // — everything below runs at ~200 ms —
class TickScheduler {
public:
    /// The single tick interval for the main update loop.
    static constexpr int TICK_INTERVAL_MS = 200;

    /// Advance the frame counter by one. Call once per frame.
    void Advance() { ++m_frame; }

    /// Returns true if at least TICK_INTERVAL_MS milliseconds of real
    /// wall-clock time have elapsed since the last time this returned true.
    /// On first call the timer is initialised and the function returns true.
    bool ShouldTick() {
        auto now = std::chrono::steady_clock::now();

        // First call — initialise and fire.
        if (m_lastTick == TimePoint{}) {
            m_lastTick = now;
            return true;
        }

        auto elapsedMs = std::chrono::duration_cast<
            std::chrono::milliseconds>(now - m_lastTick).count();
        if (elapsedMs >= TICK_INTERVAL_MS) {
            m_lastTick = now;
            return true;
        }
        return false;
    }

    /// Raw frame count (1-based after the first Advance).
    /// Useful for logging / diagnostics only.
    uint64_t FrameCount() const { return m_frame; }

private:
    using TimePoint = std::chrono::steady_clock::time_point;

    uint64_t  m_frame = 0;
    TimePoint m_lastTick{};
};

} // namespace TalosAP
