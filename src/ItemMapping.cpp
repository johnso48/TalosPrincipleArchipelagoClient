#include "headers/ItemMapping.h"

#include <algorithm>
#include <regex>
#include <DynamicOutput/DynamicOutput.hpp>

using namespace RC;

namespace TalosAP {

// ============================================================
// All tetrominoes in the game (from BotPuzzleDatabase.csv)
// Order matters — location IDs are assigned sequentially.
// ============================================================
static const std::vector<std::string> ALL_TETROMINOES = {
    // World A1 (7)
    "DJ3",  "MT1",  "DZ1",  "DJ2",  "DJ1",  "ML1",  "DI1",
    // World A2 (3)
    "ML2",  "DL1",  "DZ2",
    // World A3 (4)
    "MT2",  "DZ3",  "NL1",  "MT3",
    // World A4 (4)
    "MZ1",  "MZ2",  "MT4",  "MT5",
    // World A5 (5)
    "NZ1",  "DI2",  "DT1",  "DT2",  "DL2",
    // World A6 (4)
    "DZ4",  "NL2",  "NL3",  "NZ2",
    // World A7 (5)
    "NL4",  "DL3",  "NT1",  "NO1",  "DT3",
    // World B1 (5)
    "ML3",  "MZ3",  "MS1",  "MT6",  "MT7",
    // World B2 (4)
    "NL5",  "MS2",  "MT8",  "MZ4",
    // World B3 (4)
    "MT9",  "MJ1",  "NT2",  "NL6",
    // World B4 (6)
    "NT3",  "NT4",  "DT4",  "DJ4",  "NL7",  "NL8",
    // World B5 (5)
    "NI1",  "NL9",  "NS1",  "DJ5",  "NZ3",
    // World B6 (3)
    "NI2",  "MT10", "ML4",
    // World B7 (4)
    "NJ1",  "NI3",  "MO1",  "MI1",
    // World C1 (4)
    "NZ4",  "NJ2",  "NI4",  "NT5",
    // World C2 (4)
    "NZ5",  "NO2",  "NT6",  "NS2",
    // World C3 (4)
    "NJ3",  "NO3",  "NZ6",  "NT7",
    // World C4 (4)
    "NT8",  "NI5",  "NS3",  "NT9",
    // World C5 (4)
    "NI6",  "NO4",  "NO5",  "NT10",
    // World C6 (3)
    "NS4",  "NJ4",  "NO6",
    // World C7 (4)
    "NT11", "NO7",  "NT12", "NL10",
};

// ============================================================
// Purple Sigils (HL1 - HL24)
// ============================================================
static const std::vector<std::string> ALL_PURPLE_SIGILS = {
    "HL1",  "HL2",  "HL3",  "HL4",  "HL5",  "HL6",
    "HL7",  "HL8",  "HL9",  "HL10", "HL11", "HL12",
    "HL13", "HL14", "HL15", "HL16", "HL17", "HL18",
    "HL19", "HL20", "HL21", "HL22", "HL23", "HL24",
};

// ============================================================
// Stars (SL/SZ prefix, order matches AP world definition)
// ============================================================
static const std::vector<std::string> ALL_STARS = {
    "SL5",  "SL2",  "SZ3",  "SL1",  "SL4",  "SL7",
    "SL6",  "SZ8",  "SL9",  "SL10", "SL11", "SL12",
    "SL13", "SZ24", "SZ14", "SZ15", "SL16", "SL17",
    "SL18", "SL19", "SL20", "SL21", "SL22", "SL23",
    "SL27", "SL29", "SL30", "SZ26", "SL25", "SL28",
};

// ============================================================
// Extract the letter prefix from a tetromino ID (e.g. "DJ3" → "DJ")
// ============================================================
static std::string ExtractPrefix(const std::string& tetId)
{
    size_t i = 0;
    while (i < tetId.size() && std::isalpha(static_cast<unsigned char>(tetId[i]))) {
        ++i;
    }
    return tetId.substr(0, i);
}

// Extract the numeric suffix from a tetromino ID (e.g. "DJ3" → 3)
static int ExtractNumber(const std::string& tetId)
{
    size_t i = 0;
    while (i < tetId.size() && std::isalpha(static_cast<unsigned char>(tetId[i]))) {
        ++i;
    }
    if (i < tetId.size()) {
        return std::stoi(tetId.substr(i));
    }
    return 0;
}

// ============================================================
// Construction
// ============================================================
ItemMapping::ItemMapping()
{
    // AP item ID → prefix
    m_apItemIdToPrefix = {
        {0x540000, "DJ"},  // Green J
        {0x540001, "DZ"},  // Green Z
        {0x540002, "DI"},  // Green I
        {0x540003, "DL"},  // Green L
        {0x540004, "DT"},  // Green T
        {0x540005, "MT"},  // Golden T
        {0x540006, "ML"},  // Golden L
        {0x540007, "MZ"},  // Golden Z
        {0x540008, "MS"},  // Golden S
        {0x540009, "MJ"},  // Golden J
        {0x54000A, "MO"},  // Golden O
        {0x54000B, "MI"},  // Golden I
        {0x54000C, "NL"},  // Red L
        {0x54000D, "NZ"},  // Red Z
        {0x54000E, "NT"},  // Red T
        {0x54000F, "NI"},  // Red I
        {0x540010, "NJ"},  // Red J
        {0x540011, "NO"},  // Red O
        {0x540012, "NS"},  // Red S
        {0x540013, "HL"},  // Purple Sigil
        {0x540014, "**"},  // Star
    };

    // Display names
    m_prefixDisplayNames = {
        {"DJ", "Green J"},  {"DZ", "Green Z"},  {"DI", "Green I"},
        {"DL", "Green L"},  {"DT", "Green T"},
        {"MT", "Golden T"}, {"ML", "Golden L"}, {"MZ", "Golden Z"},
        {"MS", "Golden S"}, {"MJ", "Golden J"}, {"MO", "Golden O"},
        {"MI", "Golden I"},
        {"NL", "Red L"},    {"NZ", "Red Z"},    {"NT", "Red T"},
        {"NI", "Red I"},    {"NJ", "Red J"},    {"NO", "Red O"},
        {"NS", "Red S"},
        {"HL", "Purple Sigil"},
        {"**", "Star"},
    };

    BuildTables();
}

void ItemMapping::BuildSequences()
{
    m_tetrominoSequences.clear();

    auto addSequences = [&](const std::vector<std::string>& ids) {
        for (const auto& tetId : ids) {
            std::string prefix = ExtractPrefix(tetId);
            if (!prefix.empty()) {
                m_tetrominoSequences[prefix].push_back(tetId);
            }
        }
    };
    addSequences(ALL_TETROMINOES);
    addSequences(ALL_PURPLE_SIGILS);
    // Note: ALL_STARS is NOT added here — it's only used for location IDs.
    // Stars use a unified "**" sequence for item resolution.

    // Sort each sequence by embedded number
    for (auto& [prefix, seq] : m_tetrominoSequences) {
        std::sort(seq.begin(), seq.end(), [](const std::string& a, const std::string& b) {
            return ExtractNumber(a) < ExtractNumber(b);
        });
    }

    // Build unified star sequence: **1, **2, ..., **30
    // When AP sends a Star item, we just grant the next **N in order.
    {
        std::vector<std::string> starSeq;
        starSeq.reserve(ALL_STARS.size());
        for (int i = 1; i <= static_cast<int>(ALL_STARS.size()); ++i) {
            starSeq.push_back("**" + std::to_string(i));
        }
        m_tetrominoSequences["**"] = std::move(starSeq);
    }
}

void ItemMapping::BuildTables()
{
    BuildSequences();

    int64_t idx = 0;

    // Tetromino locations (sequential IDs)
    for (const auto& tetId : ALL_TETROMINOES) {
        int64_t locId = BASE_LOCATION_ID + idx;
        m_locationNameToId[tetId] = locId;
        m_locationIdToName[locId] = tetId;
        ++idx;
    }

    // Purple sigil locations (continue sequential IDs after tetrominoes)
    for (const auto& sigId : ALL_PURPLE_SIGILS) {
        int64_t locId = BASE_LOCATION_ID + idx;
        m_locationNameToId[sigId] = locId;
        m_locationIdToName[locId] = sigId;
        ++idx;
    }

    // Star locations (continue sequential IDs after purple sigils)
    for (const auto& starId : ALL_STARS) {
        int64_t locId = BASE_LOCATION_ID + idx;
        m_locationNameToId[starId] = locId;
        m_locationIdToName[locId] = starId;
        ++idx;
    }

    BuildGameKeyMappings();

    Output::send<LogLevel::Verbose>(STR("[TalosAP] Mappings built: {} locations, {} item types, {} game-key translations\n"),
                                    idx, m_apItemIdToPrefix.size(), m_modIdToGameKey.size());
}

void ItemMapping::BuildGameKeyMappings()
{
    m_modIdToGameKey.clear();
    m_gameKeyToModId.clear();

    // Stars use "**{number}" in the game's TMap instead of "SL{n}"/"SZ{n}".
    // The game stores Secret-type items with 0x2A ('*') for both type and shape.
    for (const auto& starId : ALL_STARS) {
        int num = ExtractNumber(starId);
        std::string gameKey = "**" + std::to_string(num);
        m_modIdToGameKey[starId] = gameKey;
        m_gameKeyToModId[gameKey] = starId;
    }
}

// ============================================================
// Item resolution
// ============================================================

std::optional<std::string> ItemMapping::ResolveNextItem(int64_t apItemId)
{
    auto it = m_apItemIdToPrefix.find(apItemId);
    if (it == m_apItemIdToPrefix.end()) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] Unknown AP item ID: {} (0x{:X})\n"),
                                        apItemId, apItemId);
        return std::nullopt;
    }

    const std::string& prefix = it->second;
    auto seqIt = m_tetrominoSequences.find(prefix);
    if (seqIt == m_tetrominoSequences.end() || seqIt->second.empty()) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] No tetromino sequence for prefix: {}\n"),
                                        std::wstring(prefix.begin(), prefix.end()));
        return std::nullopt;
    }

    const auto& seq = seqIt->second;
    int& count = m_receivedCounts[prefix];
    ++count;

    if (count > static_cast<int>(seq.size())) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] Received more {} items ({}) than exist ({}) — ignoring\n"),
                                        std::wstring(prefix.begin(), prefix.end()),
                                        count, seq.size());
        return std::nullopt;
    }

    const std::string& tetId = seq[count - 1];
    Output::send<LogLevel::Verbose>(STR("[TalosAP] Resolved AP item {} (0x{:X}) -> {} [{} {}/{}]\n"),
                                    apItemId, apItemId,
                                    std::wstring(tetId.begin(), tetId.end()),
                                    std::wstring(prefix.begin(), prefix.end()),
                                    count, seq.size());
    return tetId;
}

void ItemMapping::ResetItemCounters()
{
    m_receivedCounts.clear();
    Output::send<LogLevel::Verbose>(STR("[TalosAP] Item received counters reset\n"));
}

// ============================================================
// Location queries
// ============================================================

int64_t ItemMapping::GetLocationId(const std::string& tetrominoId) const
{
    auto it = m_locationNameToId.find(tetrominoId);
    return (it != m_locationNameToId.end()) ? it->second : -1;
}

std::string ItemMapping::GetLocationName(int64_t locationId) const
{
    auto it = m_locationIdToName.find(locationId);
    return (it != m_locationIdToName.end()) ? it->second : "";
}

std::string ItemMapping::GetDisplayName(int64_t apItemId) const
{
    auto it = m_apItemIdToPrefix.find(apItemId);
    if (it == m_apItemIdToPrefix.end()) return "";
    auto nameIt = m_prefixDisplayNames.find(it->second);
    return (nameIt != m_prefixDisplayNames.end()) ? nameIt->second : "";
}

std::string ItemMapping::GetDisplayNameForTetromino(const std::string& tetrominoId) const
{
    std::string prefix = ExtractPrefix(tetrominoId);
    auto nameIt = m_prefixDisplayNames.find(prefix);
    return (nameIt != m_prefixDisplayNames.end()) ? nameIt->second : "";
}

std::string ItemMapping::GetItemPrefix(int64_t apItemId) const
{
    auto it = m_apItemIdToPrefix.find(apItemId);
    return (it != m_apItemIdToPrefix.end()) ? it->second : "";
}

std::vector<int64_t> ItemMapping::GetAllLocationIds() const
{
    std::vector<int64_t> ids;
    ids.reserve(m_locationIdToName.size());
    for (const auto& [id, name] : m_locationIdToName) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<int64_t> ItemMapping::GetAllItemIds() const
{
    std::vector<int64_t> ids;
    ids.reserve(m_apItemIdToPrefix.size());
    for (const auto& [id, prefix] : m_apItemIdToPrefix) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool ItemMapping::IsPurpleSigil(const std::string& id)
{
    return id.size() >= 3 && id[0] == 'H' && id[1] == 'L';
}

bool ItemMapping::IsStar(const std::string& id)
{
    if (id.size() < 3) return false;
    // Stars stored in GrantedItems as "**N" (game-key format)
    if (id[0] == '*' && id[1] == '*') return true;
    // Stars referenced by location as "SL{n}" / "SZ{n}"
    if (id[0] == 'S' && (id[1] == 'L' || id[1] == 'Z')) return true;
    return false;
}

std::string ItemMapping::ToGameKey(const std::string& modId) const
{
    auto it = m_modIdToGameKey.find(modId);
    return (it != m_modIdToGameKey.end()) ? it->second : modId;
}

std::string ItemMapping::FromGameKey(const std::string& gameKey) const
{
    auto it = m_gameKeyToModId.find(gameKey);
    return (it != m_gameKeyToModId.end()) ? it->second : gameKey;
}

} // namespace TalosAP
