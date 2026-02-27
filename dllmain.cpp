#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <Mod/CppUserModBase.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Input/Handler.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>

#include "src/headers/Config.h"
#include "src/headers/ModState.h"
#include "src/headers/ItemMapping.h"
#include "src/headers/APClient.h"
#include "src/headers/InventorySync.h"
#include "src/headers/LevelTransitionHandler.h"
#include "src/headers/SaveGameHandler.h"
#include "src/headers/VisibilityManager.h"
#include "src/headers/HudNotification.h"
#include "src/headers/DeathLinkHandler.h"
#include "src/headers/GoalDetectionHandler.h"

#include <filesystem>

using namespace RC;
using namespace RC::Unreal;

class TalosPrincipleArchipelagoMod : public RC::CppUserModBase
{
public:
    TalosPrincipleArchipelagoMod() : CppUserModBase()
    {
        ModName = STR("TalosPrincipleArchipelago");
        ModVersion = STR("0.1.0");
        ModDescription = STR("Archipelago multiworld integration for The Talos Principle Reawakened");
        ModAuthors = STR("Froddo");

        Output::send<LogLevel::Verbose>(STR("[TalosAP] Mod constructed\n"));
    }

    ~TalosPrincipleArchipelagoMod() override
    {
        // Signal on_update to stop all UObject work immediately.
        // During engine teardown UObjects are freed while our tick
        // is still running — any FindAllOf / FindFirstOf call will
        // crash with an access violation (SEH, not catchable by C++).
        // Note: The ReceiveShutdown hook should have already set this
        // flag before UObject teardown began; this is a safety net.
        m_shuttingDown.store(true);
    }

    // ============================================================
    // on_unreal_init — Unreal Engine is ready, safe to use UE types
    // ============================================================
    auto on_unreal_init() -> void override
    {
        Output::send<LogLevel::Verbose>(STR("[TalosAP] on_unreal_init — initializing...\n"));

        // Load configuration
        // Try to find the mod directory (where the DLL lives)
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

        // Initialize item mapping
        m_itemMapping = std::make_unique<TalosAP::ItemMapping>();
        Output::send<LogLevel::Verbose>(STR("[TalosAP] Item mappings built\n"));

        // Initialize HUD notification overlay
        m_hud = std::make_unique<TalosAP::HudNotification>();
        m_hud->SetShutdownFlag(&m_shuttingDown);
        if (m_hud->Init()) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] HUD notification system initialized\n"));
        } else {
            Output::send<LogLevel::Warning>(STR("[TalosAP] HUD init deferred — UMG classes not yet available\n"));
        }

        // Initialize AP client (unless offline mode)
        if (!m_config.offline_mode) {
            m_apClient = std::make_unique<TalosAP::APClientWrapper>();
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

        // ============================================================
        // Register debug key bindings
        // ============================================================
        register_keydown_event(Input::Key::F6, [this]() {
            m_state.PendingInventoryDump.store(true);
        });

        // F9: Test HUD notifications — fires one of each color type
        register_keydown_event(Input::Key::F9, [this]() {
            m_state.PendingHudTest.store(true);
        });

        // F10: Debug spawn mine at player location
        register_keydown_event(Input::Key::F10, [this]() {
            m_state.PendingDebugSpawnMine.store(true);
        });

        // ============================================================
        // Register hooks
        // ============================================================
        m_levelTransitionHandler.RegisterHooks(m_state);
        m_saveGameHandler.RegisterHooks(m_state);
        m_deathLinkHandler.RegisterHooks(m_state);
        m_goalDetectionHandler.Init(m_state);

        // ============================================================
        // Hook: KismetSystemLibrary::QuitGame
        // ============================================================
        try {
            UObjectGlobals::RegisterHook(
                STR("/Script/Engine.KismetSystemLibrary:QuitGame"),
                [](UnrealScriptFunctionCallableContext& ctx, void* data) {
                    auto* flag = static_cast<std::atomic<bool>*>(data);
                    flag->store(true);
                    Output::send<LogLevel::Verbose>(STR("[TalosAP] Hook: QuitGame — disabling all UObject work\n"));
                },
                {},
                &m_shuttingDown
            );
            Output::send<LogLevel::Verbose>(STR("[TalosAP] Hooked: KismetSystemLibrary::QuitGame\n"));
        }
        catch (...) {
            Output::send<LogLevel::Warning>(STR("[TalosAP] Failed to hook QuitGame\n"));
        }

        Output::send<LogLevel::Verbose>(STR("[TalosAP] Initialization complete\n"));
    }

    // ============================================================
    // on_update — called every tick from the game thread
    // ============================================================
    auto on_update() -> void override
    {
        // Bail immediately if the engine is tearing down.
        // UObjects may already be freed — any FindAllOf/FindFirstOf
        // call would be an access violation.
        if (m_shuttingDown.load()) return;

        ++m_tickCount;

        // Poll AP client for network events
        if (m_apClient) {
            m_apClient->Poll();
        }

        // Tick HUD notification system (~12 ticks)
        if (m_hud && (m_tickCount % 12 == 0)) {
            m_hud->Tick();
        }

        // Level transition cooldown — real wall-clock time (10 s)
        if (m_state.IsInLevelTransitionCooldown()) {
            m_levelTransitionCooldownWasActive = true;
            return; // Skip all game-thread work during transitions
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

        // Deferred progress refresh
        if (m_state.NeedsProgressRefresh) {
            m_state.NeedsProgressRefresh = false;
            TalosAP::InventorySync::FindProgressObject(m_state, true);
            if (m_state.CurrentProgress) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] Deferred progress refresh complete\n"));
            }
        }

        // ============================================================
        // DeathLink: process incoming death (every tick, low cost)
        // ============================================================
        if (m_state.DeathLinkEnabled) {
            m_deathLinkHandler.ProcessPendingDeathLink(m_state, m_hud.get());
        }

        // ============================================================
        // DeathLink: send outgoing death
        // ============================================================
        if (m_state.PendingDeathLinkSend.exchange(false)) {
            if (m_apClient && m_state.DeathLinkEnabled) {
                m_apClient->SendDeathLink("Died in The Talos Principle");
            }
        }

        // F6: inventory dump
        if (m_state.PendingInventoryDump.exchange(false)) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] === F6 Inventory Dump ===\n"));
            TalosAP::InventorySync::FindProgressObject(m_state);
            TalosAP::InventorySync::DumpCollectedTetrominos(m_state, *m_itemMapping);
            m_visibilityManager.DumpTracked();
            m_visibilityManager.DumpFenceMap();
        }

        // F9: HUD notification test
        if (m_state.PendingHudTest.exchange(false) && m_hud) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] === F9: HUD notification test ===\n"));
            m_hud->Notify({
                { L"Alice",         TalosAP::HudColors::PLAYER },
                { L" sent you a ",   TalosAP::HudColors::WHITE  },
                { L"Red L",         TalosAP::HudColors::TRAP   },
            });
            m_hud->Notify({
                { L"Bob",           TalosAP::HudColors::PLAYER },
                { L" sent you a ",   TalosAP::HudColors::WHITE  },
                { L"Golden T",      TalosAP::HudColors::PROGRESSION },
            });
            m_hud->Notify({
                { L"You found a ",   TalosAP::HudColors::WHITE  },
                { L"Green J",       TalosAP::HudColors::ITEM   },
            });
            m_hud->NotifySimple(L"AP Connected to server", TalosAP::HudColors::SERVER);
        }

        // ============================================================
        // F10: Debug spawn mine at player
        // ============================================================
        if (m_state.PendingDebugSpawnMine.exchange(false)) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] === F10: Debug spawn mine ===\n"));
            DebugSpawnMineAtPlayer();
        }

        // ============================================================
        // Tetromino scan — run once after level transitions
        // ============================================================
        if (m_state.NeedsTetrominoScan) {
            m_state.NeedsTetrominoScan = false;
            m_visibilityManager.ResetCache();
            m_visibilityManager.ScanLevel(m_state);
        }

        // ============================================================
        // Visibility enforcement + proximity pickup (every 5 ticks)
        // Rate-limited: EnforceVisibility calls FindAllOf + iterates
        // all actors, too expensive to run every frame. 5 ticks at
        // 60fps ≈ 12 Hz, still responsive for player proximity.
        // ============================================================
        if (m_state.APSynced && m_itemMapping && (m_tickCount % 5 == 0)) {
            m_visibilityManager.EnforceVisibility(m_state, *m_itemMapping,
                [this](int64_t locationId) {
                    if (m_apClient) {
                        m_apClient->SendLocationCheck(locationId);
                    }
                });
        }

        // ============================================================
        // Periodic full visibility refresh (every ~60 ticks / ~1s)
        // Re-discovers actors, rebuilds tracked positions, reapplies
        // visibility. Keeps tracking data current after items arrive.
        // ============================================================
        if (m_tickCount % 60 == 0) {
            m_visibilityManager.RefreshVisibility(m_state);
        }

        // ============================================================
        // Process pending fence opens (every ~6 ticks / ~100ms)
        // Retries ALoweringFence::Open() with 100ms spacing, up to 10x
        // ============================================================
        if (m_tickCount % 6 == 0) {
            m_visibilityManager.ProcessPendingFenceOpens();
        }

        // Enforce collection state every ~60 ticks
        if (m_tickCount % 60 == 0) {
            // Always re-acquire the progress object — cached UObject* can go
            // stale at any time due to Unreal GC.
            TalosAP::InventorySync::FindProgressObject(m_state);
            if (m_state.CurrentProgress) {
                TalosAP::InventorySync::EnforceCollectionState(m_state, *m_itemMapping);
            }
        }

        // ============================================================
        // Goal detection — deferred hooks + polling
        // Tick handles the full lifecycle: warmup → hook registration → polling.
        // ============================================================
        if (m_tickCount % 60 == 0) {
            m_goalDetectionHandler.Tick(m_state);
        }

        // ============================================================
        // Goal completion: send to AP server (once)
        // ============================================================
        if (m_goalDetectionHandler.IsGoalCompleted() && !m_goalSent) {
            m_goalSent = true;
            if (m_hud) {
                std::wstring msg = L"Goal Complete: " +
                    std::wstring(m_goalDetectionHandler.GetCompletedGoalName().begin(),
                                 m_goalDetectionHandler.GetCompletedGoalName().end());
                m_hud->NotifySimple(msg, TalosAP::HudColors::SERVER);
            }
            if (m_apClient) {
                m_apClient->SendGoalComplete();
            }
        }
    }

private:
    // ============================================================
    // DebugSpawnMineAtPlayer — F10 debug helper
    // ============================================================
    // Attempts to spawn or teleport a mine right on top of the
    // player so we can observe what happens.  Heavy logging.
    void DebugSpawnMineAtPlayer()
    {
        try {
            // ── 1. Find player pawn ────────────────────────────
            auto* pc = UObjectGlobals::FindFirstOf(STR("PlayerController"));
            if (!pc) {
                Output::send<LogLevel::Warning>(STR("[TalosAP-Debug] No PlayerController\n"));
                return;
            }
            Output::send<LogLevel::Verbose>(STR("[TalosAP-Debug] Found PlayerController\n"));

            auto* pawnPtr = pc->GetValuePtrByPropertyNameInChain<UObject*>(STR("Pawn"));
            if (!pawnPtr || !*pawnPtr) {
                Output::send<LogLevel::Warning>(STR("[TalosAP-Debug] No Pawn\n"));
                return;
            }
            UObject* pawn = *pawnPtr;
            Output::send<LogLevel::Verbose>(STR("[TalosAP-Debug] Found Pawn at {}\n"), (void*)pawn);

            // ── 2. Get player location ─────────────────────────
            double px = 0, py = 0, pz = 0;
            bool haveLocation = false;

            // Method A: Read RootComponent->RelativeLocation property directly
            try {
                auto* rootCompPtr = pawn->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootComponent"));
                if (rootCompPtr && *rootCompPtr) {
                    Output::send<LogLevel::Verbose>(STR("[TalosAP-Debug] Found RootComponent at {}\n"), (void*)*rootCompPtr);
                    auto* relLoc = (*rootCompPtr)->GetValuePtrByPropertyNameInChain<double>(STR("RelativeLocation"));
                    if (relLoc) {
                        px = relLoc[0];
                        py = relLoc[1];
                        pz = relLoc[2];
                        haveLocation = true;
                        Output::send<LogLevel::Verbose>(STR("[TalosAP-Debug] Location via RelativeLocation: ({}, {}, {})\n"), px, py, pz);
                    } else {
                        Output::send<LogLevel::Verbose>(STR("[TalosAP-Debug] RelativeLocation property not found\n"));
                    }
                } else {
                    Output::send<LogLevel::Verbose>(STR("[TalosAP-Debug] RootComponent not found\n"));
                }
            }
            catch (...) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP-Debug] Exception reading RootComponent->RelativeLocation\n"));
            }

            // ── 3. Find existing mines in the level ────────────
            const wchar_t* mineClasses[] = {
                STR("BP_Mine_C"),
                STR("BP_PassiveMine_C"),
            };

            std::vector<UObject*> foundMines;
            const wchar_t* foundClassName = nullptr;
            for (auto* cls : mineClasses) {
                try {
                    UObjectGlobals::FindAllOf(cls, foundMines);
                    if (!foundMines.empty()) {
                        foundClassName = cls;
                        break;
                    }
                } catch (...) {}
            }

            if (!foundMines.empty()) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP-Debug] Found {} mine(s) of class '{}'\n"),
                    foundMines.size(), std::wstring(foundClassName));

                // Log mine positions via property reads (same pattern as VisibilityManager)
                for (size_t i = 0; i < foundMines.size() && i < 3; ++i) {
                    try {
                        auto* mRoot = foundMines[i]->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootComponent"));
                        if (mRoot && *mRoot) {
                            auto* mLoc = (*mRoot)->GetValuePtrByPropertyNameInChain<double>(STR("RelativeLocation"));
                            if (mLoc) {
                                Output::send<LogLevel::Verbose>(STR("[TalosAP-Debug] Mine[{}] at ({}, {}, {})\n"),
                                    i, mLoc[0], mLoc[1], mLoc[2]);
                            } else {
                                Output::send<LogLevel::Verbose>(STR("[TalosAP-Debug] Mine[{}] no RelativeLocation\n"), i);
                            }
                        } else {
                            Output::send<LogLevel::Verbose>(STR("[TalosAP-Debug] Mine[{}] no RootComponent\n"), i);
                        }
                    } catch (...) {
                        Output::send<LogLevel::Verbose>(STR("[TalosAP-Debug] Mine[{}] exception reading location\n"), i);
                    }
                }
            } else {
                Output::send<LogLevel::Verbose>(STR("[TalosAP-Debug] No mines found in current level\n"));
            }

            // ── 4. Teleport mine to player ────────────────────────
            // Write RelativeLocation directly + flush via SetAbsolute toggle.
            // This bypasses Angelscript entirely — the game's own tick will
            // detect the overlap and trigger the mine's kill sequence.
            if (!foundMines.empty() && haveLocation) {
                UObject* mine = foundMines[0];
                bool moved = false;

                try {
                    auto* mRootPtr = mine->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootComponent"));
                    if (mRootPtr && *mRootPtr) {
                        UObject* mRoot = *mRootPtr;
                        Output::send<LogLevel::Verbose>(STR("[TalosAP-Debug] Mine RootComponent at {}\n"), (void*)mRoot);

                        // Write RelativeLocation directly
                        auto* mLoc = mRoot->GetValuePtrByPropertyNameInChain<double>(STR("RelativeLocation"));
                        if (mLoc) {
                            mLoc[0] = px;
                            mLoc[1] = py;
                            mLoc[2] = pz;
                            Output::send<LogLevel::Verbose>(STR("[TalosAP-Debug] Wrote RelativeLocation = ({}, {}, {})\n"), px, py, pz);

                            // Flush transform via SetAbsolute toggle
                            auto* fnSetAbsolute = mRoot->GetFunctionByNameInChain(STR("SetAbsolute"));
                            if (fnSetAbsolute) {
                                {
                                    alignas(16) uint8_t params[4] = {};
                                    params[0] = 1; // bAbsoluteLocation = true
                                    mRoot->ProcessEvent(fnSetAbsolute, params);
                                }
                                {
                                    alignas(16) uint8_t params[4] = {};
                                    params[0] = 0; // bAbsoluteLocation = false
                                    mRoot->ProcessEvent(fnSetAbsolute, params);
                                }
                                moved = true;
                                Output::send<LogLevel::Verbose>(STR("[TalosAP-Debug] SetAbsolute toggle succeeded — mine teleported\n"));
                            } else {
                                Output::send<LogLevel::Warning>(STR("[TalosAP-Debug] SetAbsolute not found on RootComponent\n"));
                            }
                        } else {
                            Output::send<LogLevel::Warning>(STR("[TalosAP-Debug] Mine RelativeLocation property not found\n"));
                        }
                    } else {
                        Output::send<LogLevel::Warning>(STR("[TalosAP-Debug] Mine RootComponent not found\n"));
                    }
                }
                catch (...) {
                    Output::send<LogLevel::Warning>(STR("[TalosAP-Debug] Exception in mine teleport\n"));
                }

                if (!moved) {
                    Output::send<LogLevel::Warning>(STR("[TalosAP-Debug] Could not teleport mine to player\n"));
                }
            }

        }
        catch (...) {
            Output::send<LogLevel::Warning>(STR("[TalosAP-Debug] Outer exception in DebugSpawnMineAtPlayer\n"));
        }
    }

    TalosAP::Config                            m_config;
    TalosAP::ModState                          m_state;
    std::unique_ptr<TalosAP::ItemMapping>      m_itemMapping;
    std::unique_ptr<TalosAP::APClientWrapper>  m_apClient;
    std::unique_ptr<TalosAP::HudNotification>  m_hud;
    TalosAP::LevelTransitionHandler            m_levelTransitionHandler;
    TalosAP::SaveGameHandler                   m_saveGameHandler;
    TalosAP::DeathLinkHandler                  m_deathLinkHandler;
    TalosAP::VisibilityManager                 m_visibilityManager;
    TalosAP::GoalDetectionHandler              m_goalDetectionHandler;
    uint64_t                                   m_tickCount = 0;
    std::atomic<bool>                           m_shuttingDown{false};
    bool                                       m_goalSent = false;
    bool                                       m_levelTransitionCooldownWasActive = false;
};

// ============================================================
// DLL Exports
// ============================================================
#define TALOS_AP_MOD_API __declspec(dllexport)
extern "C"
{
    TALOS_AP_MOD_API RC::CppUserModBase* start_mod()
    {
        return new TalosPrincipleArchipelagoMod();
    }

    TALOS_AP_MOD_API void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}
