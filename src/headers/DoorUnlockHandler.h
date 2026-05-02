#pragma once

#include "ModState.h"

namespace TalosAP {

/// Handles writing door-unlock variables into save data when
/// PendingDoorUnlockEnforce is set (by AP item grants).
///
/// For each door ID in ModState::UnlockedDoors, calls
/// TalosProgress::BoolSet("Unlocked_<doorId>", true) so the
/// game's own BeginPlay scripts open the doors on level load.
///
/// Gate opening at runtime is handled by giving the player the
/// required Door-type tetromino pieces (via InventorySync) so
/// they can solve the arranger manually.
class DoorUnlockHandler {
public:
    /// Process a pending door-unlock enforcement on the game thread.
    /// Called every tick from ModCore::Tick().
    void ProcessPending(ModState& state);
};

} // namespace TalosAP
