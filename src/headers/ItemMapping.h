#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <optional>

namespace TalosAP {

/// Categorisation of AP item IDs.
enum class ItemCategory {
    Tetromino,   ///< Standard tetromino / sigil / star / bonus puzzle
    Mechanic,    ///< Puzzle-mechanic unlock (Connector, Hexahedron, etc.)
    Gate,        ///< World gate unlock (A1, A, B, C)
    Unknown,     ///< Unrecognised item
};

/// Maps between Archipelago item/location IDs and in-game tetromino IDs.
///
/// AP uses 19 item types (one per shape+color combo). Each type maps to a
/// prefix (e.g. "DJ" = Green J). When duplicates are received, they resolve
/// to the next tetromino in sequence (DJ1, DJ2, DJ3...).
///
/// Locations are 1:1 with physical tetrominos, purple sigils, and stars in the game world.
class ItemMapping {
public:
    static constexpr int64_t BASE_ITEM_ID     = 0x540000; // 5505024
    static constexpr int64_t BASE_LOCATION_ID = 0x540000; // 5505024

    // New special item IDs
    static constexpr int64_t ITEM_CONNECTOR   = 0x54001C; // Rod mechanic
    static constexpr int64_t ITEM_HEXAHEDRON  = 0x54001D; // Cube mechanic
    static constexpr int64_t ITEM_FANS        = 0x54001E; // Fan mechanic
    static constexpr int64_t ITEM_PLAYBACK    = 0x54001F; // Time mechanic
    static constexpr int64_t ITEM_PLATFORM    = 0x540020; // Platform mechanic
    static constexpr int64_t ITEM_GATE_A1     = 0x540021; // World A1 Gate
    static constexpr int64_t ITEM_GATE_A      = 0x540022; // World A Gate
    static constexpr int64_t ITEM_GATE_B      = 0x540023; // World B Gate
    static constexpr int64_t ITEM_GATE_C      = 0x540024; // World C Gate

    ItemMapping();

    /// Classify an AP item ID into Tetromino / Mechanic / Gate / Unknown.
    ItemCategory ClassifyItem(int64_t apItemId) const;

    /// For a Mechanic item, return the EPuzzleMechanic bitmask value.
    /// Returns 0 if the item is not a mechanic.
    uint8_t GetMechanicBit(int64_t apItemId) const;

    /// For a Gate item, return the internal door arranger ID
    /// (e.g. "DoorTutorial"). Returns empty if not a gate item.
    std::string GetGateDoorId(int64_t apItemId) const;

    /// For a Gate item, return the Door-type tetromino pieces required
    /// to solve that gate's arranger. Each pair is {AP_item_id, count}.
    /// The AP item IDs correspond to Green (Door) tetromino types.
    std::vector<std::pair<int64_t, int>> GetGateRequiredPieces(int64_t apItemId) const;

    /// Resolve the next concrete tetromino for a received AP item.
    /// Increments per-prefix counter. Returns empty if exhausted/unknown.
    std::optional<std::string> ResolveNextItem(int64_t apItemId);

    /// Reset received-item counters. Must be called on (re)connect before
    /// the AP server replays all received items.
    void ResetItemCounters();

    /// Get the AP location ID for a tetromino ID. Returns -1 if unknown.
    int64_t GetLocationId(const std::string& tetrominoId) const;

    /// Get the tetromino ID for an AP location ID. Returns empty if unknown.
    std::string GetLocationName(int64_t locationId) const;

    /// Get the human-readable display name for an AP item ID (e.g. "Green J").
    std::string GetDisplayName(int64_t apItemId) const;

    /// Get the display name for a tetromino ID string (e.g. "DJ3" → "Green J").
    std::string GetDisplayNameForTetromino(const std::string& tetrominoId) const;

    /// Get the shape+color prefix for an AP item ID (e.g. 0x540000 → "DJ").
    std::string GetItemPrefix(int64_t apItemId) const;

    /// Get all location IDs as a sorted vector.
    std::vector<int64_t> GetAllLocationIds() const;

    /// Get all AP item type IDs.
    std::vector<int64_t> GetAllItemIds() const;

    /// Returns true if the given ID is a purple sigil (HL prefix).
    static bool IsPurpleSigil(const std::string& id);

    /// Returns true if the given ID is a star (SL or SZ prefix).
    static bool IsStar(const std::string& id);

    /// Returns true if the given ID is a bonus puzzle (ES, EL, or EO prefix).
    static bool IsBonusPuzzle(const std::string& id);

    /// Convert a mod-internal ID (e.g. "SL5") to the game's TMap key format
    /// (e.g. "**5"). Returns the input unchanged for non-star IDs.
    std::string ToGameKey(const std::string& modId) const;

    /// Convert a game TMap key (e.g. "**5") back to the mod-internal ID
    /// (e.g. "SL5"). Returns the input unchanged for non-star keys.
    std::string FromGameKey(const std::string& gameKey) const;

private:
    /// AP item ID → prefix (e.g. 0x540000 → "DJ")
    std::unordered_map<int64_t, std::string> m_apItemIdToPrefix;

    /// Display names for special (non-tetromino) items (mechanic unlocks, gates)
    std::unordered_map<int64_t, std::string> m_specialItemDisplayNames;

    /// Prefix → display name (e.g. "DJ" → "Green J")
    std::unordered_map<std::string, std::string> m_prefixDisplayNames;

    /// Prefix → ordered sequence of tetromino IDs (e.g. "DJ" → {"DJ1","DJ2","DJ3","DJ4","DJ5"})
    std::unordered_map<std::string, std::vector<std::string>> m_tetrominoSequences;

    /// Tetromino/sigil ID → AP location ID
    std::unordered_map<std::string, int64_t> m_locationNameToId;

    /// AP location ID → tetromino/sigil ID
    std::unordered_map<int64_t, std::string> m_locationIdToName;

    /// Per-prefix received count (how many of each type AP has sent)
    std::unordered_map<std::string, int> m_receivedCounts;

    /// Mod ID → game TMap key (e.g. "SL5" → "**5"). Only populated for
    /// IDs whose TMap encoding differs from the mod's letter-based format.
    std::unordered_map<std::string, std::string> m_modIdToGameKey;

    /// Game TMap key → mod ID (reverse of above).
    std::unordered_map<std::string, std::string> m_gameKeyToModId;

    void BuildTables();
    void BuildSequences();
    void BuildGameKeyMappings();
};

} // namespace TalosAP
