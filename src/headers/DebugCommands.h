#pragma once

#include "ModState.h"
#include "ItemMapping.h"
#include "VisibilityManager.h"
#include "HudNotification.h"

namespace TalosAP {

/// Processes debug key commands (F6 inventory dump, F9 HUD test).
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

    /// F6: Dump save-game / progress state for all arrangers in the current level.
    /// Shows IsArrangerSolved, IsPuzzleSolved, progress variables, and the
    /// full GetDebugString() output from UTalosProgress.
    static void DumpSaveGameState(ModState& state);
};

} // namespace TalosAP
