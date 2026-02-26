#include "headers/DeathLinkHandler.h"

#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

#include <vector>

using namespace RC;
using namespace RC::Unreal;

namespace TalosAP {

// ============================================================
// Helper — read an actor's world position via RootComponent->RelativeLocation.
// This is the ONLY reliable method — ProcessEvent throws on Angelscript actors.
// ============================================================
static bool ReadActorPosition(UObject* actor, double& outX, double& outY, double& outZ)
{
    if (!actor) return false;
    auto* rootPtr = actor->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootComponent"));
    if (!rootPtr || !*rootPtr) return false;
    auto* loc = (*rootPtr)->GetValuePtrByPropertyNameInChain<double>(STR("RelativeLocation"));
    if (!loc) return false;
    outX = loc[0];
    outY = loc[1];
    outZ = loc[2];
    return true;
}

// ============================================================
// TeleportMineTo — move a mine actor to (x,y,z) bypassing Angelscript.
//
// K2_SetRelativeLocation (the normal UFUNCTION route) calls
// UpdateOverlaps() synchronously, which fires Angelscript overlap
// callbacks and throws in many levels.  Instead we:
//
// 1. Write the target position directly to
//    RootComponent->RelativeLocation (pure property write — always works).
//
// 2. Call SetAbsolute(true, false, false) on the RootComponent.
//    SetAbsolute is a native USceneComponent UFUNCTION that calls
//    UpdateComponentToWorld() to recompute the world-space transform
//    from RelativeLocation — but does NOT call UpdateOverlaps().
//    This avoids triggering Angelscript delegates entirely.
//
// 3. Call SetAbsolute(false, false, false) to restore the original
//    flags (forces another UpdateComponentToWorld, still no overlaps).
//
// The mine's own ReceiveTick / physics tick will detect the player
// overlap naturally on the very next frame and start the kill.
// ============================================================
static bool TeleportMineTo(UObject* mine, double x, double y, double z)
{
    if (!mine) return false;

    // 1. Get RootComponent
    auto* rootPtr = mine->GetValuePtrByPropertyNameInChain<UObject*>(STR("RootComponent"));
    if (!rootPtr || !*rootPtr) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] TeleportMineTo: no RootComponent\n"));
        return false;
    }
    UObject* root = *rootPtr;

    // 2. Write RelativeLocation directly (3 × double = FVector)
    auto* loc = root->GetValuePtrByPropertyNameInChain<double>(STR("RelativeLocation"));
    if (!loc) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] TeleportMineTo: no RelativeLocation property\n"));
        return false;
    }
    loc[0] = x;
    loc[1] = y;
    loc[2] = z;

    // 3. Find SetAbsolute on the RootComponent (native USceneComponent)
    auto* fnSetAbsolute = root->GetFunctionByNameInChain(STR("SetAbsolute"));
    if (!fnSetAbsolute) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] TeleportMineTo: SetAbsolute not found\n"));
        return false;
    }

    // SetAbsolute(bool bNewAbsoluteLocation, bool bNewAbsoluteRotation, bool bNewAbsoluteScale)
    // Toggle bAbsoluteLocation on then off to guarantee UpdateComponentToWorld fires
    // (SetAbsolute early-outs if flags are unchanged).
    {
        alignas(16) uint8_t params[4] = {};
        params[0] = 1;  // bNewAbsoluteLocation = true
        params[1] = 0;  // bNewAbsoluteRotation = false
        params[2] = 0;  // bNewAbsoluteScale = false
        root->ProcessEvent(fnSetAbsolute, params);
    }
    {
        alignas(16) uint8_t params[4] = {};
        params[0] = 0;  // bNewAbsoluteLocation = false
        params[1] = 0;  // bNewAbsoluteRotation = false
        params[2] = 0;  // bNewAbsoluteScale = false
        root->ProcessEvent(fnSetAbsolute, params);
    }

    Output::send<LogLevel::Verbose>(STR("[TalosAP] TeleportMineTo: Moved mine to ({}, {}, {})\n"), x, y, z);
    return true;
}

// ============================================================
// RegisterHooks — hook ATalosCharacter::SetDeath to detect deaths
// ============================================================
void DeathLinkHandler::RegisterHooks(ModState& state)
{
    try {
        auto hookId = UObjectGlobals::RegisterHook(
            STR("/Script/Talos.TalosCharacter:SetDeath"),
            [](UnrealScriptFunctionCallableContext& ctx, void* data) {
                auto* st = static_cast<ModState*>(data);

                if (!st->DeathLinkEnabled) return;

                // If this death was caused by an incoming DeathLink
                // (e.g. a mine we teleported), consume the flag and
                // suppress the outgoing bounce.
                if (st->IsDeathLinkDeath) {
                    st->IsDeathLinkDeath = false;   // one-shot guard
                    return;
                }

                Output::send<LogLevel::Verbose>(STR("[TalosAP] DeathLink: Player died — queueing outgoing DeathLink\n"));
                st->PendingDeathLinkSend.store(true);
            },
            {},
            &state
        );
        m_hookIds.push_back(hookId);
        Output::send<LogLevel::Verbose>(STR("[TalosAP] DeathLink: Hooked TalosCharacter:SetDeath\n"));
    }
    catch (...) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] DeathLink: Failed to hook TalosCharacter:SetDeath — trying blueprint event\n"));

        try {
            auto hookId = UObjectGlobals::RegisterHook(
                STR("/Game/Talos/Blueprints/Characters/BP_TalosCharacter.BP_TalosCharacter_C:OnTalosPlayerDied_Event"),
                [](UnrealScriptFunctionCallableContext& ctx, void* data) {
                    auto* st = static_cast<ModState*>(data);

                    if (!st->DeathLinkEnabled) return;
                    if (st->IsDeathLinkDeath) {
                        st->IsDeathLinkDeath = false;
                        return;
                    }

                    Output::send<LogLevel::Verbose>(STR("[TalosAP] DeathLink: Player died (BP event) — queueing outgoing DeathLink\n"));
                    st->PendingDeathLinkSend.store(true);
                },
                {},
                &state
            );
            m_hookIds.push_back(hookId);
            Output::send<LogLevel::Verbose>(STR("[TalosAP] DeathLink: Hooked BP_TalosCharacter:OnTalosPlayerDied_Event\n"));
        }
        catch (...) {
            Output::send<LogLevel::Warning>(STR("[TalosAP] DeathLink: Failed to hook any death event — outgoing DeathLinks will not work\n"));
        }
    }
}

// ============================================================
// ProcessPendingDeathLink — inflict death from incoming DeathLink
// ============================================================
//
// DESIGN NOTE:  Talos Principle has no health/damage system.
// Deaths are instant (mine explosion, turret beam, water, etc.).
// All game death functions (SetDeath, Dead, HandlePuzzleDeath) are
// Angelscript-bridged and crash when called via ProcessEvent from
// UE4SS.
//
// We teleport an existing mine onto the player using TeleportMineTo:
// direct property write to RootComponent->RelativeLocation followed
// by a SetAbsolute toggle to flush the world transform via
// UpdateComponentToWorld (no overlap checks, no Angelscript).
// The game's own tick naturally detects the mine on the player.
//
// If no mine exists in the current level, we show an ominous HUD
// message and set PendingDeferredDeathLink so the update loop will
// re-attempt after the next level transition.
// ============================================================
void DeathLinkHandler::ProcessPendingDeathLink(ModState& state, HudNotification* hud)
{
    if (!state.PendingDeathLinkReceive.exchange(false)) return;

    Output::send<LogLevel::Verbose>(STR("[TalosAP] DeathLink: Processing incoming death from '{}'\n"),
        std::wstring(state.DeathLinkSource.begin(), state.DeathLinkSource.end()));

    // ── Find player pawn ───────────────────────────────────────
    UObject* pawn = nullptr;
    try {
        auto* pc = UObjectGlobals::FindFirstOf(STR("PlayerController"));
        if (!pc) {
            Output::send<LogLevel::Warning>(STR("[TalosAP] DeathLink: No PlayerController found\n"));
            return;
        }
        auto* pawnPtr = pc->GetValuePtrByPropertyNameInChain<UObject*>(STR("Pawn"));
        if (!pawnPtr || !*pawnPtr) {
            Output::send<LogLevel::Warning>(STR("[TalosAP] DeathLink: No Pawn found\n"));
            return;
        }
        pawn = *pawnPtr;
    }
    catch (...) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] DeathLink: Exception finding player\n"));
        return;
    }

    // Skip if already dead
    try {
        auto* isDead = pawn->GetValuePtrByPropertyNameInChain<bool>(STR("bIsDead"));
        if (isDead && *isDead) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] DeathLink: Player already dead — skipping\n"));
            return;
        }
    }
    catch (...) {}

    // Get the player's current location
    double playerX = 0, playerY = 0, playerZ = 0;
    bool haveLocation = false;
    try {
        haveLocation = ReadActorPosition(pawn, playerX, playerY, playerZ);
        if (haveLocation) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] DeathLink: Player at ({}, {}, {})\n"),
                playerX, playerY, playerZ);
        }
    }
    catch (...) {
        Output::send<LogLevel::Verbose>(STR("[TalosAP] DeathLink: Failed to get player location\n"));
    }

    // ── Find a mine in the current level ───────────────────────
    UObject* mine = nullptr;
    if (haveLocation) {
        const wchar_t* mineClasses[] = {
            STR("BP_Mine_C"),
            STR("BP_PassiveMine_C"),
        };
        for (auto* cls : mineClasses) {
            std::vector<UObject*> mines;
            try { UObjectGlobals::FindAllOf(cls, mines); } catch (...) { continue; }
            if (!mines.empty()) {
                mine = mines[0];
                Output::send<LogLevel::Verbose>(STR("[TalosAP] DeathLink: Found mine of class '{}'\n"),
                    std::wstring(cls));
                break;
            }
        }
    }

    // ── Kill the player by teleporting a mine onto them ─────────
    bool killed = false;
    if (mine && haveLocation) {
        state.IsDeathLinkDeath = true;   // consumed by SetDeath hook later

        try {
            if (TeleportMineTo(mine, playerX, playerY, playerZ)) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] DeathLink: Teleported mine to player\n"));
                killed = true;
            }
        }
        catch (...) {
            Output::send<LogLevel::Warning>(STR("[TalosAP] DeathLink: TeleportMineTo threw\n"));
        }

        if (!killed) {
            state.IsDeathLinkDeath = false;
        }
    }

    // ── No mine available — defer death to next level ──────────
    if (!killed) {
        Output::send<LogLevel::Verbose>(STR("[TalosAP] DeathLink: No mines in current level — deferring to next level\n"));
        
        // Ominous HUD message
        if (hud && !state.PendingDeferredDeathLink) {
            hud->Notify({
                { L"Death",               HudColors::TRAP   },
                { L" is coming for you.", HudColors::WHITE  },
            });
        }

        state.PendingDeferredDeathLink = true;
    }

    // ── HUD notification (when death was inflicted) ────────────
    if (killed && hud) {
        std::wstring wSource(state.DeathLinkSource.begin(), state.DeathLinkSource.end());
        std::wstring wCause(state.DeathLinkCause.begin(), state.DeathLinkCause.end());

        if (!wCause.empty()) {
            hud->Notify({
                { wCause,   HudColors::TRAP   }
            });
        } else {
            hud->Notify({
                { wSource,          HudColors::PLAYER },
                { L" killed you!",  HudColors::TRAP   },
            });
        }
    }
}

} // namespace TalosAP
