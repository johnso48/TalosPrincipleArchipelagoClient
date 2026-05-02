#pragma once

#include "ModState.h"

#include <atomic>
#include <vector>
#include <utility>

namespace TalosAP {

/// Selectively unlocks puzzle mechanics (Rods, Cubes, Fans, etc.)
/// via a two-layer approach:
///
/// **Save-data layer** — marks the mechanic-unlock arranger as solved
/// by calling StoreTetrominoLocationsForArranger() + SetArrangerSolved()
/// on UTalosProgress, so IsMechanicUnlocked() returns true.
///
/// **Runtime layer** — clears RequiredMechanics bits (offset 0x02A0)
/// on every CarriableComponent so items are usable immediately.
///
/// **Hook layer** — post-hooks on PuzzleMemoryFunctions::IsMechanicUnlocked
/// and CarriableComponent::IsMechanicUnlocked override the return value
/// to fix the puzzle-entry HUD red flash.
class MechanicsHandler {
public:

    /// Process a pending mechanics patch on the game thread.
    /// Called every tick from ModCore::Tick().
    void ProcessPending(ModState& state);

private:
    std::vector<std::pair<int, int>> m_hookIds;
};

} // namespace TalosAP
