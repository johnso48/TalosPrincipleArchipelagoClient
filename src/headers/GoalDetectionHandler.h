#pragma once

#include "ModState.h"

#include <vector>
#include <utility>
#include <string>
#include <chrono>

namespace TalosAP {

/// Tracks completion of Archipelago goals by hooking in-game ending triggers.
///
/// Transcendence ending — three detection strategies (first wins):
///   1. RegisterHook on Ending_Transcendence_DirectorBP_C:SequenceEvent_0
///      (succeeds only if the class is already loaded).
///   2. Polling via StaticFindObject for the LevelSequence asset
///      /Game/Cinematics/Sequences/Endings/Ending_Transcendence.
///
/// Ascension ending — two hook strategies + polling:
///   1. Hook BinkMediaPlayer:OpenUrl and match "Ending_Ascension".
///   2. Hook BinkMediaPlayer:OnBinkMediaPlayerMediaOpened.
///   3. Poll BinkMediaPlayer objects for SequentialMediaPlayer_Secondary
///      with a URL containing "Ending_Ascension".
///
/// Fallback: TalosSaveSubsystem:IsGameCompleted polling.
///
/// Hook registration is deferred by HOOK_DELAY_MS (20 s) after Init()
/// because BinkMediaPlayer and cinematics classes are not loaded during
/// early engine init — RegisterHook on them triggers a native access
/// violation (SEH, uncatchable by C++ try/catch).
///
/// The handler exposes GoalCompleted / CompletedGoalName which are read by
/// the update loop to trigger APClientWrapper::SendGoalComplete().
class GoalDetectionHandler {
public:
    /// Number of granted sigils required for Transcendence goal.
    static constexpr int TRANSCENDENCE_SIGIL_REQUIREMENT = 90;

    /// Additional wall-clock delay (ms) after hooks are registered
    /// before polling starts.
    static constexpr int POLL_DELAY_MS = 20000;    // 20 seconds

    /// Store the ModState pointer and record the start time.
    /// Call from on_unreal_init.  Does NOT register hooks immediately —
    /// that happens in Tick() after HOOK_DELAY_MS.
    void Init(ModState& state);

    /// Called periodically from on_update.
    /// Handles the full lifecycle: warmup → hook registration → polling.
    void Tick(ModState& state);

    /// Whether a goal has been completed this session.
    bool IsGoalCompleted() const { return m_goalCompleted; }

    /// Name of the completed goal ("Transcendence" / "Ascension" / "Unknown").
    const std::string& GetCompletedGoalName() const { return m_completedGoalName; }

    /// Reset goal state (for new game / slot switch).
    void ResetGoalState();

private:
    /// Register hooks for ending detection. Called internally once
    /// HOOK_DELAY_MS has elapsed.
    void RegisterHooks();

    /// Poll-based goal checking. Called internally once hooks are
    /// registered and POLL_DELAY_MS has elapsed.
    void CheckGoals(ModState& state);

    /// Internal: fire goal completion (deduplicated).
    void FireGoal(const std::string& goalName, const std::string& source);

    /// Count the number of items currently granted by AP.
    static int GetGrantedSigilCount(const ModState& state);

    /// Check if the player has enough sigils for Transcendence.
    static bool HasEnoughSigils(const ModState& state);

    std::vector<std::pair<int, int>> m_hookIds;

    ModState*   m_state            = nullptr;
    bool        m_goalCompleted    = false;
    std::string m_completedGoalName;

    /// True if the startup RegisterHook for Transcendence failed
    /// (expected — the class only loads when the ending plays).
    bool m_transcendenceHookFailed = false;

    /// Whether hooks have been registered.
    bool m_hooksRegistered = false;

    /// Whether polling is active.
    bool m_pollingActive = false;

    /// Wall-clock time_point after which polling should start.
    std::chrono::steady_clock::time_point m_pollReadyTime{};

    /// Previous value of IsGameCompleted to detect rising edge.
    bool m_previousGameCompleted = false;

    /// Last polled BinkMediaPlayer URL (for change detection).
    std::wstring m_lastPolledUrl;
};

} // namespace TalosAP
