#define NOMINMAX
#include <windows.h>

#include "headers/ModCore.h"
#include "headers/InventorySync.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>

#include <filesystem>

using namespace RC;
using namespace RC::Unreal;

namespace TalosAP {

// ============================================================
// Initialize — called once from on_unreal_init
// ============================================================
void ModCore::Initialize(std::atomic<bool>& shuttingDown)
{
    m_shuttingDown = &shuttingDown;
    m_workQueue.SetShutdownFlag(&shuttingDown);
    Output::send<LogLevel::Verbose>(STR("[TalosAP] ModCore::Initialize — starting...\n"));

    LoadConfig(shuttingDown);
    InitSubsystems(shuttingDown);
    RegisterHooks(shuttingDown);

    Output::send<LogLevel::Verbose>(STR("[TalosAP] Initialization complete\n"));
}

// ============================================================
// LoadConfig — resolve DLL path and load config.json
// ============================================================
void ModCore::LoadConfig(std::atomic<bool>& /*shuttingDown*/)
{
    std::wstring modDir;
    try {
        wchar_t dllPath[MAX_PATH];
        HMODULE hModule = nullptr;
        // Use a static dummy variable whose address lives inside this DLL
        static const int s_anchor = 0;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&s_anchor),
                           &hModule);
        if (hModule) {
            GetModuleFileNameW(hModule, dllPath, MAX_PATH);
            std::filesystem::path p(dllPath);
            // DLL is in Mods/<ModName>/dlls/main.dll, config is in Mods/<ModName>/
            modDir = p.parent_path().parent_path().wstring();
        }
    }
    catch (...) {}

    m_config.Load(modDir);
    Output::send<LogLevel::Verbose>(STR("[TalosAP] Config loaded\n"));
}

// ============================================================
// InitSubsystems — create ItemMapping, HUD, AP client
// ============================================================
void ModCore::InitSubsystems(std::atomic<bool>& shuttingDown)
{
    // Item mapping
    m_itemMapping = std::make_unique<ItemMapping>();
    Output::send<LogLevel::Verbose>(STR("[TalosAP] Item mappings built\n"));

    // HUD notification overlay
    if (m_config.enable_hud) {
        m_hud = std::make_unique<HudNotification>();
        m_hud->SetShutdownFlag(&shuttingDown);
        if (m_hud->Init()) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] HUD notification system initialized\n"));
        } else {
            Output::send<LogLevel::Warning>(STR("[TalosAP] HUD init deferred \u2014 UMG classes not yet available\n"));
        }
    } else {
        Output::send<LogLevel::Warning>(STR("[TalosAP] HUD DISABLED by config\n"));
    }

    // AP client (unless offline mode)
    if (!m_config.offline_mode) {
        m_apClient = std::make_unique<APClientWrapper>();
        bool ok = m_apClient->Init(m_config, m_state, *m_itemMapping, m_hud.get());
        if (ok) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] AP client initialized — connection will start on poll\n"));
        } else {
            Output::send<LogLevel::Error>(STR("[TalosAP] AP client initialization failed\n"));
            m_apClient.reset();
        }
    } else {
        Output::send<LogLevel::Verbose>(STR("[TalosAP] Offline mode — AP client disabled\n"));
        m_state.APSynced = true; // Enable enforcement immediately in offline mode
    }
}

// ============================================================
// RegisterHooks — level transition, save game, death link, etc.
// ============================================================
void ModCore::RegisterHooks(std::atomic<bool>& shuttingDown)
{
    if (m_config.enable_level_transition_hooks) {
        m_levelTransitionHandler.RegisterHooks(m_state);
    } else {
        Output::send<LogLevel::Warning>(STR("[TalosAP] LevelTransitionHandler hooks DISABLED by config\n"));
    }

    if (m_config.enable_save_hooks) {
        m_saveGameHandler.RegisterHooks(m_state);
    } else {
        Output::send<LogLevel::Warning>(STR("[TalosAP] SaveGameHandler hooks DISABLED by config\n"));
    }

    if (m_config.death_link) {
        m_deathLinkHandler.RegisterHooks(m_state);
    }

    if (m_config.enable_goal_detection) {
        m_goalDetectionHandler.Init(m_state);
    } else {
        Output::send<LogLevel::Warning>(STR("[TalosAP] GoalDetectionHandler DISABLED by config\n"));
    }

    // Hook: KismetSystemLibrary::QuitGame
    try {
        UObjectGlobals::RegisterHook(
            STR("/Script/Engine.KismetSystemLibrary:QuitGame"),
            [](UnrealScriptFunctionCallableContext& ctx, void* data) {
                auto* flag = static_cast<std::atomic<bool>*>(data);
                flag->store(true);
                Output::send<LogLevel::Verbose>(STR("[TalosAP] Hook: QuitGame — disabling all UObject work\n"));
            },
            {},
            &shuttingDown
        );
        Output::send<LogLevel::Verbose>(STR("[TalosAP] Hooked: KismetSystemLibrary::QuitGame\n"));
    }
    catch (...) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] Failed to hook QuitGame\n"));
    }
}

// ============================================================
// Tick — per-frame update, called from on_update
// ============================================================
void ModCore::Tick()
{
    // Bail immediately if the engine is tearing down.
    if (m_shuttingDown && m_shuttingDown->load()) return;

    if (m_apPollThrottle.Ready()) PollAPClient();

    // Level transition cooldown blocks all game-thread subsystems.
    if (HandleLevelTransitionCooldown()) return;

    // ---- Flag-gated one-shots (cheap no-ops when flags are clear) ----
    if (m_config.enable_inventory_sync) ProcessDeferredProgressRefresh();
    ProcessDeathLinks();

    if (m_config.enable_debug_commands) {
        m_debugCommands.ProcessPending(m_state, *m_itemMapping,
                                       m_visibilityManager, m_hud.get());
    }

    if (m_config.tetromino_handling.scan_for_new_tetrominos) {
        ProcessTetrominoScan();
    }

    // ---- Throttled subsystems: enqueue when ready, execute one per frame ----
    EnqueueThrottledWork();
    m_workQueue.ProcessOne();
}

// ============================================================
// EnqueueThrottledWork — check each throttle and push ready work
// ============================================================
void ModCore::EnqueueThrottledWork()
{
    if (m_config.enable_hud && m_hud && m_hudThrottle.Ready()) {
        m_workQueue.Enqueue(Work_Hud, [this] {
            m_hud->Tick();
        });
    }

    if (m_visibilityThrottle.Ready()) {
        m_workQueue.Enqueue(Work_Visibility, [this] {
            if (m_config.tetromino_handling.enforce_visibility
                || m_config.tetromino_handling.player_proximity_pickup) {
                EnforceVisibilityAndPickups();
            }
            if (m_config.tetromino_handling.enforce_visibility) {
                RefreshVisibility();
            }
        });
    }

    if (m_config.tetromino_handling.fence_opening && m_fenceThrottle.Ready()) {
        m_workQueue.Enqueue(Work_FenceOpen, [this] {
            ProcessPendingFenceOpens();
        });
    }

    if (m_config.enable_inventory_sync && m_inventorySyncThrottle.Ready()) {
        m_workQueue.Enqueue(Work_InventorySync, [this] {
            EnforceCollectionState();
        });
    }

    if (m_config.enable_goal_detection && m_goalThrottle.Ready()) {
        m_workQueue.Enqueue(Work_GoalDetection, [this] {
            TickGoalDetection();
            CheckGoalCompletion();
        });
    }
}

// ============================================================
// Tick sub-steps
// ============================================================

void ModCore::PollAPClient()
{
    if (m_apClient) {
        m_apClient->Poll();
    }
}

bool ModCore::HandleLevelTransitionCooldown()
{
    // Level transition cooldown — real wall-clock time
    if (m_state.IsInLevelTransitionCooldown()) {
        m_levelTransitionCooldownWasActive = true;
        m_workQueue.Clear(); // Discard stale work from the previous level
        return true; // Skip all game-thread work during transitions
    }

    // Fire once when cooldown has just expired
    if (m_levelTransitionCooldownWasActive) {
        m_levelTransitionCooldownWasActive = false;
        Output::send<LogLevel::Verbose>(STR("[TalosAP] Level transition cooldown expired — resuming\n"));

        // If a DeathLink was deferred because the previous level
        // had no mines, re-trigger it now that we're in a new level.
        if (m_state.PendingDeferredDeathLink && m_state.DeathLinkEnabled) {
            m_state.PendingDeferredDeathLink = false;
            m_state.PendingDeathLinkReceive.store(true);
            Output::send<LogLevel::Verbose>(STR("[TalosAP] DeathLink: Re-triggering deferred death in new level\n"));
        }
    }

    return false;
}

void ModCore::ProcessDeferredProgressRefresh()
{
    if (m_state.NeedsProgressRefresh) {
        m_state.NeedsProgressRefresh = false;
        InventorySync::FindProgressObject(m_state, true);
        if (m_state.CurrentProgress) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] Deferred progress refresh complete\n"));
        }
    }
}

void ModCore::ProcessDeathLinks()
{
    // Process incoming death (every tick, low cost)
    if (m_state.DeathLinkEnabled) {
        m_deathLinkHandler.ProcessPendingDeathLink(m_state, m_hud.get());
    }

    // Send outgoing death
    if (m_state.PendingDeathLinkSend.exchange(false)) {
        if (m_apClient && m_state.DeathLinkEnabled) {
            m_apClient->SendDeathLink("Died in The Talos Principle");
        }
    }
}

void ModCore::ProcessTetrominoScan()
{
    if (m_state.NeedsTetrominoScan) {
        m_state.NeedsTetrominoScan = false;
        m_visibilityManager.ResetCache();
        m_visibilityManager.ScanLevel(m_state);
    }
}

void ModCore::EnforceVisibilityAndPickups()
{
    if (m_state.APSynced && m_itemMapping) {
        m_visibilityManager.EnforceVisibility(m_state, *m_itemMapping,
            [this](int64_t locationId) {
                if (m_apClient) {
                    m_apClient->SendLocationCheck(locationId);
                }
            },
            m_config.tetromino_handling.enforce_visibility,
            m_config.tetromino_handling.player_proximity_pickup);
    }
}

void ModCore::RefreshVisibility()
{
    m_visibilityManager.RefreshVisibility(m_state);
}

void ModCore::ProcessPendingFenceOpens()
{
    m_visibilityManager.ProcessPendingFenceOpens();
}

void ModCore::EnforceCollectionState()
{
    // Always re-acquire the progress object — cached UObject* can go
    // stale at any time due to Unreal GC.
    InventorySync::FindProgressObject(m_state);
    if (m_state.CurrentProgress) {
        InventorySync::EnforceCollectionState(m_state, *m_itemMapping);
    }
}

void ModCore::TickGoalDetection()
{
    m_goalDetectionHandler.Tick(m_state);
}

void ModCore::CheckGoalCompletion()
{
    if (m_goalDetectionHandler.IsGoalCompleted() && !m_goalSent) {
        m_goalSent = true;
        if (m_hud) {
            std::wstring msg = L"Goal Complete: " +
                std::wstring(m_goalDetectionHandler.GetCompletedGoalName().begin(),
                             m_goalDetectionHandler.GetCompletedGoalName().end());
            m_hud->NotifySimple(msg, HudColors::SERVER);
        }
        if (m_apClient) {
            m_apClient->SendGoalComplete();
        }
    }
}

} // namespace TalosAP
