#pragma once

#include "ModState.h"
#include "ItemMapping.h"

#include <string>
#include <unordered_set>

namespace TalosAP {

/// Manages synchronization between the AP-granted items and the
/// in-game CollectedTetrominos TMap on UTalosProgress.
///
/// Key operations:
///   GrantItem     — add a tetromino to the TMap
///   EnforceState  — ensure TMap matches GrantedItems (remove non-granted, add granted)
///   RefreshUI     — notify arrangers and HUD widgets of inventory changes
class InventorySync {
public:
    /// Find the active UTalosProgress object and cache it in state.
    /// Uses UTalosProgress::Get(WorldContext) as primary strategy,
    /// with character fallback.
    static void FindProgressObject(ModState& state, bool forceRefresh = false);

    /// Grant an item — add to GrantedItems and TMap.
    static void GrantItem(ModState& state, const std::string& tetrominoId);

    /// Revoke an item — remove from GrantedItems and TMap.
    static void RevokeItem(ModState& state, const std::string& tetrominoId);

    /// Enforce collection state: sync TMap with GrantedItems.
    /// Removes non-granted items, ensures granted items are present.
    /// Items not recognised by ItemMapping are left untouched.
    /// Blocked until state.APSynced is true.
    static void EnforceCollectionState(ModState& state, const ItemMapping& itemMapping);

    /// Refresh the in-game tetromino UI (arranger panels, HUD counters).
    static void RefreshUI();

    /// Dump current TMap contents to the log for debugging.
    static void DumpCollectedTetrominos(ModState& state, const ItemMapping& itemMapping);

    /// Enforce puzzle-solved state: for each checked location, call
    /// BoolSet("Puzzle::<id>", true) on UTalosProgress so that sign
    /// posts show the black X mark. Uses a hardcoded mapping from
    /// BotPuzzleDatabase.csv to resolve tetromino → puzzle ID.
    static void EnforcePuzzleSolvedState(ModState& state, const ItemMapping& itemMapping);

    /// Reset cached UFunction pointers (call on level transition).
    static void ResetCachedFunctions();

private:
    /// Set of checked-location tetromino IDs already marked as solved this session.
    /// Avoids redundant BoolSet calls every enforcement tick.
    static std::unordered_set<std::string> s_solvedPuzzleCache;
};

} // namespace TalosAP
