#pragma once

#include "Config.h"
#include "ModState.h"
#include "ItemMapping.h"
#include "APClient.h"
#include "HudNotification.h"
#include "LevelTransitionHandler.h"
#include "SaveGameHandler.h"
#include "DeathLinkHandler.h"
#include "VisibilityManager.h"
#include "GoalDetectionHandler.h"
#include "Throttle.h"
#include "ThrottledWorkQueue.h"
#include "DebugCommands.h"

#include <memory>
#include <atomic>

namespace TalosAP {

/// Core mod logic — owns all subsystems and orchestrates their lifecycle.
///
/// Extracted from the CppUserModBase subclass so that dllmain.cpp is a
/// thin DLL shell and all game logic lives in testable, well-separated
/// translation units.
///
/// Public surface:
///   Initialize() — called once from on_unreal_init
///   Tick()       — called every frame from on_update
///   OnKeyF6/F9() — immediate key-press handlers (set atomic flags)
class ModCore {
public:
    /// Set up all subsystems, load config, register hooks.
    /// @param shuttingDown  Atomic flag owned by the DLL class; when set
    ///                      to true all UObject work is skipped.
    void Initialize(std::atomic<bool>& shuttingDown);

    /// Per-frame update — polls AP, enforces visibility, etc.
    /// Returns immediately when the shutdown flag is set.
    void Tick();

    /// Immediate key-press handlers (called from key-down lambdas).
    void OnKeyF6() { m_state.PendingInventoryDump.store(true); }
    void OnKeyF7() { m_state.PendingOpenDoorArrangers.store(true); }
    void OnKeyF9() { m_state.PendingHudTest.store(true); }

private:
    // ---- Initialization sub-steps ----
    void LoadConfig(std::atomic<bool>& shuttingDown);
    void InitSubsystems(std::atomic<bool>& shuttingDown);
    void RegisterHooks(std::atomic<bool>& shuttingDown);

    // ---- Work-queue tags (one per throttled subsystem) ----
    enum WorkTag {
        Work_Hud,
        Work_Visibility,
        Work_FenceOpen,
        Work_InventorySync,
        Work_GoalDetection,
    };

    // ---- Tick sub-steps (called from Tick()) ----
    void PollAPClient();
    bool HandleLevelTransitionCooldown();   ///< returns true → skip rest of tick
    void ProcessDeferredProgressRefresh();
    void ProcessDeathLinks();
    void ProcessTetrominoScan();
    void EnqueueThrottledWork();            ///< check throttles and enqueue ready work
    void EnforceVisibilityAndPickups();
    void RefreshVisibility();
    void ProcessPendingFenceOpens();
    void EnforceCollectionState();
    void TickGoalDetection();
    void CheckGoalCompletion();

    // ---- Subsystems ----
    Config                            m_config;
    ModState                          m_state;
    std::unique_ptr<ItemMapping>      m_itemMapping;
    std::unique_ptr<APClientWrapper>  m_apClient;
    std::unique_ptr<HudNotification>  m_hud;
    LevelTransitionHandler            m_levelTransitionHandler;
    SaveGameHandler                   m_saveGameHandler;
    DeathLinkHandler                  m_deathLinkHandler;
    VisibilityManager                 m_visibilityManager;
    GoalDetectionHandler              m_goalDetectionHandler;
    DebugCommands                     m_debugCommands;

    // ---- Per-subsystem throttles (wall-clock, in ms) ----
    Throttle                          m_apPollThrottle{500};
    Throttle                          m_hudThrottle{200};
    Throttle                          m_visibilityThrottle{200};
    Throttle                          m_fenceThrottle{200};
    Throttle                          m_inventorySyncThrottle{200};
    Throttle                          m_goalThrottle{1000};

    // ---- FIFO work queue (executes one item per frame) ----
    ThrottledWorkQueue                m_workQueue;

    // ---- State ----
    std::atomic<bool>*                m_shuttingDown = nullptr;
    bool                              m_goalSent = false;
    bool                              m_levelTransitionCooldownWasActive = false;
};

} // namespace TalosAP
