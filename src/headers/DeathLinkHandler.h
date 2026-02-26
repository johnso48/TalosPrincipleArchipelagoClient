#pragma once

#include "ModState.h"
#include "HudNotification.h"

#include <vector>
#include <utility>

namespace TalosAP {

/// Handles Archipelago DeathLink — both sending death notifications when the
/// local player dies, and killing the local player when a DeathLink bounce
/// is received from another player.
///
/// Death detection:
///   Hooks ATalosCharacter::SetDeath to detect when the player dies in-game.
///   The hook sets PendingDeathLinkSend in ModState (unless the death was
///   itself caused by an incoming DeathLink).
///
/// Death infliction:
///   ProcessPendingDeathLink() is called every tick from the game thread.
///   When PendingDeathLinkReceive is set, it calls SetDeath(Mine, true) on
///   the player character via ProcessEvent, causing the mine-explosion death.
class DeathLinkHandler {
public:
    /// Register the SetDeath hook. Must be called after Unreal is
    /// initialised (i.e. inside on_unreal_init).
    void RegisterHooks(ModState& state);

    /// Process a pending incoming DeathLink death on the game thread.
    /// Called every tick from on_update. If PendingDeathLinkReceive is set,
    /// finds the player character and calls SetDeath(Mine).
    void ProcessPendingDeathLink(ModState& state, HudNotification* hud);

private:
    std::vector<std::pair<int, int>> m_hookIds;
};

} // namespace TalosAP
