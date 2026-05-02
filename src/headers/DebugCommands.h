#pragma once

#include "ModState.h"
#include "ItemMapping.h"
#include "VisibilityManager.h"
#include "HudNotification.h"

namespace TalosAP {

/// Processes debug key commands (F6 inventory dump, F7 unlock mechanics, F9 HUD test).
///
/// These blocks are entirely self-contained diagnostic helpers.
/// Extracting them removes clutter from the main tick loop.
class DebugCommands {
public:
    /// Check and process any pending debug commands.
    /// Call once per tick from the main update loop.
    void ProcessPending(ModState& state,
                        ItemMapping& itemMapping,
                        VisibilityManager& visibilityManager,
                        HudNotification* hud);

private:

    /// Diagnostic: dump arranger actor state via Unreal reflection (no hardcoded offsets).
    /// Called before and after SolveArranger to detect state changes that may cause
    /// crashes on level teardown (active timelines, tweens, audio, widget refs, etc.).
    static void DumpArrangerState(RC::Unreal::UObject* arranger,
                                  const std::wstring& puzzleId,
                                  const wchar_t* label);

    /// Diagnostic: scan ArrangerOpensDoorScript, LoweringFence, and related actors
    /// to log door/fence/audio state after solve.  Helps identify which actors
    /// hold stale references during level transitions.
    static void DumpRelatedActors();

    /// F6: Dump save-game / progress state for all arrangers in the current level.
    /// Shows IsArrangerSolved, IsPuzzleSolved, progress variables, and the
    /// full GetDebugString() output from UTalosProgress.
    static void DumpSaveGameState(ModState& state);
};

} // namespace TalosAP
