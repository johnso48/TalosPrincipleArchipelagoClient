#include "headers/GoalDetectionHandler.h"

#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/Core/Containers/FString.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

#include <vector>
#include <string>

using namespace RC;
using namespace RC::Unreal;

namespace TalosAP {

// ============================================================
// Helper: count items granted by Archipelago
// ============================================================
int GoalDetectionHandler::GetGrantedSigilCount(const ModState& state)
{
    return static_cast<int>(state.GrantedItems.size());
}

bool GoalDetectionHandler::HasEnoughSigils(const ModState& state)
{
    return GetGrantedSigilCount(state) >= TRANSCENDENCE_SIGIL_REQUIREMENT;
}

// ============================================================
// FireGoal — deduplicated goal completion
// ============================================================
void GoalDetectionHandler::FireGoal(const std::string& goalName, const std::string& source)
{
    if (m_goalCompleted) return;

    Output::send<LogLevel::Verbose>(STR("[TalosAP] ======================================\n"));
    Output::send<LogLevel::Verbose>(STR("[TalosAP] GOAL: {} ACHIEVED!\n"),
        std::wstring(goalName.begin(), goalName.end()));
    Output::send<LogLevel::Verbose>(STR("[TalosAP] Source: {}\n"),
        std::wstring(source.begin(), source.end()));
    Output::send<LogLevel::Verbose>(STR("[TalosAP] ======================================\n"));

    m_goalCompleted = true;
    m_completedGoalName = goalName;
}

// ============================================================
// Init — store state pointer and start the deferred warmup clock
// ============================================================
void GoalDetectionHandler::Init(ModState& state)
{
    m_state = &state;
    auto now = std::chrono::steady_clock::now();
    m_pollReadyTime = now + std::chrono::milliseconds(POLL_DELAY_MS);
    Output::send<LogLevel::Verbose>(STR("[TalosAP] Goal: Init — polling will start in {} ms\n"), POLL_DELAY_MS);
}

// ============================================================
// Tick — lifecycle: warmup → hook registration → polling
// Called periodically from on_update.
// ============================================================
void GoalDetectionHandler::Tick(ModState& state)
{
    if (m_goalCompleted) return;

    auto now = std::chrono::steady_clock::now();

    // Phase 2: wait for poll delay
    if (!m_pollingActive) {
        if (now < m_pollReadyTime) return;
        m_pollingActive = true;
        Output::send<LogLevel::Verbose>(STR("[TalosAP] Goal: Polling active\n"));
    }

    // Phase 3: poll for goals
    CheckGoals(state);
}

// ============================================================
// CheckGoals — polling-based goal detection
// ============================================================
void GoalDetectionHandler::CheckGoals(ModState& state)
{
    if (m_goalCompleted) return;

    // ---------------------------------------------------------
    // Poll BinkMediaPlayer objects for Ascension ending movies.
    // The engine may set the URL property directly (bypassing
    // OpenUrl), so we iterate all BinkMediaPlayer instances and
    // check for SequentialMediaPlayer_Secondary with a URL
    // containing "Ending_Ascension".
    // ---------------------------------------------------------
    try {
        std::vector<UObject*> allPlayers;
        UObjectGlobals::FindAllOf(STR("BinkMediaPlayer"), allPlayers);

        for (auto* player : allPlayers) {
            if (!player) continue;

            std::wstring fullName;
            try {
                fullName = player->GetFullName();
            }
            catch (...) { continue; }

            if (fullName.find(STR("SequentialMediaPlayer_Secondary")) == std::wstring::npos) {
                continue;
            }

            // Try reading the URL property. BinkMediaPlayer stores it as an FString.
            // Try multiple property names since the exact name may vary.
            const wchar_t* urlPropNames[] = { STR("Url"), STR("URL"), STR("CurrentUrl") };

            for (auto* propName : urlPropNames) {
                try {
                    auto* urlPtr = player->GetValuePtrByPropertyNameInChain<FString>(propName);
                    if (!urlPtr) continue;

                    const wchar_t* raw = **urlPtr;
                    if (!raw) continue;

                    std::wstring url(raw);
                    if (url.empty()) continue;

                    if (url != m_lastPolledUrl) {
                        m_lastPolledUrl = url;
                        Output::send<LogLevel::Verbose>(STR("[TalosAP] Goal: SequentialMediaPlayer_Secondary URL: {}\n"), url);

                        if (url.find(STR("Ending_Ascension")) != std::wstring::npos) {
                            FireGoal("Ascension", "BinkMediaPlayer URL poll");
                            return;
                        }
                    }
                    break; // Found a URL property — no need to try others
                }
                catch (...) { continue; }
            }
        }
    }
    catch (...) {
        // FindAllOf may fail if BinkMediaPlayer class doesn't exist
    }

    // ---------------------------------------------------------
    // Transcendence: StaticFindObject for the LevelSequence.
    // Only exists in memory when the ending package is loaded
    // (i.e. the player triggered the ending). This covers both
    // the hook-failed case AND the NotifyOnNewObject replacement.
    // Always poll — even if the hook registered, it may not fire
    // reliably in all scenarios.
    // ---------------------------------------------------------
    try {
        auto* seq = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr,
            STR("/Game/Cinematics/Sequences/Endings/Ending_Transcendence.Ending_Transcendence"));

        if (seq && HasEnoughSigils(state)) {
            FireGoal("Transcendence", "Polling — Ending_Transcendence LevelSequence found in memory");
            return;
        }
    }
    catch (...) {
        // StaticFindObject may not be available or throw
    }

    // ---------------------------------------------------------
    // Secondary fallback: TalosSaveSubsystem:IsGameCompleted
    // ---------------------------------------------------------
    try {
        auto* subsystem = UObjectGlobals::FindFirstOf(STR("TalosSaveSubsystem"));
        if (subsystem) {
            auto* fn = subsystem->GetFunctionByNameInChain(STR("IsGameCompleted"));
            if (fn) {
                // IsGameCompleted returns bool — use a small param buffer
                struct { bool ReturnValue = false; } params;
                subsystem->ProcessEvent(fn, &params);

                if (params.ReturnValue && !m_previousGameCompleted) {
                    FireGoal("Unknown (polling fallback)", "TalosSaveSubsystem:IsGameCompleted");
                }
                m_previousGameCompleted = params.ReturnValue;
            }
        }
    }
    catch (...) {
        // TalosSaveSubsystem may not exist in all contexts
    }
}

// ============================================================
// ResetGoalState — for new game / slot switch
// ============================================================
void GoalDetectionHandler::ResetGoalState()
{
    m_goalCompleted = false;
    m_completedGoalName.clear();
    m_previousGameCompleted = false;
    m_lastPolledUrl.clear();
    m_hooksRegistered = false;
    m_pollingActive = false;
    Output::send<LogLevel::Verbose>(STR("[TalosAP] Goal state reset\n"));
}

} // namespace TalosAP
