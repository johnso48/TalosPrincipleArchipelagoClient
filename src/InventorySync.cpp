#include "headers/InventorySync.h"

#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FProperty.hpp>
#include <Unreal/Core/Containers/Map.hpp>
#include <Unreal/Core/Containers/FString.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

#include <vector>
#include <string>
#include <unordered_set>
#include <excpt.h>

using namespace RC;
using namespace RC::Unreal;

namespace TalosAP {

// Static member definition
std::unordered_set<std::string> InventorySync::s_solvedPuzzleCache;

// ============================================================
// Helper: Convert narrow string to FString (wide)
// ============================================================
static FString ToFString(const std::string& s)
{
    std::wstring wide(s.begin(), s.end());
    return FString(wide.c_str());
}

// Convert FString to narrow std::string
static std::string FromFString(const FString& fs)
{
    const wchar_t* wstr = *fs;
    if (!wstr) return "";
    std::string result;
    while (*wstr) {
        result.push_back(static_cast<char>(*wstr));
        ++wstr;
    }
    return result;
}

// ============================================================
// TMap access helpers
// ============================================================

// The CollectedTetrominos TMap is TMap<FString, bool>.
// We access it via GetValuePtrByPropertyNameInChain to get the raw TMap pointer,
// then use the TMap API directly since we know the concrete types.
using TetrominoMap = TMap<FString, bool>;

static TetrominoMap* GetCollectedTetrominosMap(UObject* progress)
{
    if (!progress) return nullptr;

    try {
        auto* ptr = progress->GetValuePtrByPropertyNameInChain<TetrominoMap>(STR("CollectedTetrominos"));
        return ptr;
    }
    catch (...) {
        return nullptr;
    }
}

// ============================================================
// SEH-safe wrappers
// ============================================================
// UE4SS's FindFirstOf iterates the global UObject array and calls
// IsA() on every entry, including pending-kill objects whose class
// hierarchy pointers may already be stale.  This causes an access
// violation (SEH) that C++ try/catch cannot intercept.
// Wrap every UObject-touching call in __try/__except.

static UObject* SEH_FindFirstOf(const wchar_t* className)
{
    __try {
        return UObjectGlobals::FindFirstOf(className);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static UObject* SEH_StaticFindObject(const wchar_t* path)
{
    __try {
        return UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, path);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static bool SEH_ProcessEvent(UObject* obj, UFunction* fn, void* params)
{
    __try {
        obj->ProcessEvent(fn, params);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static UFunction* SEH_GetFunctionByName(UObject* obj, const wchar_t* name)
{
    __try {
        return obj->GetFunctionByNameInChain(name);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static UFunction* SEH_StaticFindFunction(const wchar_t* path)
{
    __try {
        return UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, path);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// ============================================================
// FindProgressObject
// ============================================================

void InventorySync::FindProgressObject(ModState& state, bool /*forceRefresh*/)
{
    // Always re-acquire the progress object from scratch.
    // Cached UObject* pointers can become stale at any time due to
    // Unreal GC, and accessing a stale pointer is an access violation
    // (SEH) that try/catch cannot intercept.
    state.CurrentProgress = nullptr;

    auto* cdo = SEH_StaticFindObject(STR("/Script/Talos.Default__TalosProgress"));

    UObject* worldCtx = nullptr;

    // Try PlayerController as world context
    worldCtx = SEH_FindFirstOf(STR("PlayerController"));
    if (!worldCtx) {
        // Fallback: GameInstance
        worldCtx = SEH_FindFirstOf(STR("TalosGameInstance"));
    }

    if (cdo && worldCtx) {
        // Call UTalosProgress::Get(WorldContextObject)
        auto* getFunc = SEH_GetFunctionByName(cdo, STR("Get"));
        if (getFunc) {
            struct {
                UObject* WorldContextObject;
                UObject* ReturnValue;
            } params{};
            params.WorldContextObject = worldCtx;
            params.ReturnValue = nullptr;

            if (SEH_ProcessEvent(cdo, getFunc, &params) && params.ReturnValue) {
                // Verify we can read the TMap
                auto* tmap = GetCollectedTetrominosMap(params.ReturnValue);
                if (tmap) {
                    state.CurrentProgress = params.ReturnValue;
                    return;
                }
            }
        }
    }

    Output::send<LogLevel::Warning>(STR("[TalosAP] Could not find progress object\n"));
}

// ============================================================
// GrantItem
// ============================================================

void InventorySync::GrantItem(ModState& state, const std::string& tetrominoId)
{
    bool wasNew = (state.GrantedItems.insert(tetrominoId).second);

    if (wasNew) {
        Output::send<LogLevel::Verbose>(STR("[TalosAP] Item granted: {}\n"),
            std::wstring(tetrominoId.begin(), tetrominoId.end()));
    }

    // Don't touch the TMap here — EnforceCollectionState will sync it
    // on the next periodic pass. Accessing state.CurrentProgress here
    // risks hitting a stale pointer.

    if (wasNew) {
        RefreshUI();
    }
}

// ============================================================
// RevokeItem
// ============================================================

void InventorySync::RevokeItem(ModState& state, const std::string& tetrominoId)
{
    state.GrantedItems.erase(tetrominoId);
    state.CheckedLocations.erase(tetrominoId);

    Output::send<LogLevel::Verbose>(STR("[TalosAP] Item revoked: {}\n"),
        std::wstring(tetrominoId.begin(), tetrominoId.end()));

    // Don't touch the TMap here — EnforceCollectionState will sync it
    // on the next periodic pass.
}

// ============================================================
// EnforceCollectionState
// ============================================================

void InventorySync::EnforceCollectionState(ModState& state, const ItemMapping& itemMapping)
{
    if (!state.CurrentProgress) return;
    if (!state.APSynced) return;

    auto* tmap = GetCollectedTetrominosMap(state.CurrentProgress);
    if (!tmap) return;

    // Phase 1: Find items in TMap that are NOT granted — these must be removed.
    //          Items that are not recognised by ItemMapping are left untouched
    //          so we don't strip content added by the base game or other mods.
    //
    //          TMap keys use the game's native encoding (e.g. stars are "**5"
    //          not "SL5"). We translate via FromGameKey before checking.
    std::vector<std::string> toRemoveGameKeys;
    try {
        for (auto& pair : *tmap) {
            std::string gameKey = FromFString(pair.Key);
            if (gameKey.empty()) continue;

            // Translate game TMap key to mod ID
            std::string modId = itemMapping.FromGameKey(gameKey);

            // Skip items we don't recognise — they aren't ours to manage
            if (itemMapping.GetLocationId(modId) < 0) continue;

            // Skip purple sigils if they are not randomised
            if (!state.RandomisePurpleSigils && ItemMapping::IsPurpleSigil(modId)) continue;

            // Skip stars if they are not randomised
            if (!state.RandomiseStars && ItemMapping::IsStar(modId)) continue;

            // Skip bonus puzzles if they are not randomised
            if (!state.RandomiseBonusPuzzles && ItemMapping::IsBonusPuzzle(modId)) continue;

            // Stars are stored in GrantedItems as game keys ("**N"),
            // not as mod IDs ("SL5"). Check accordingly.
            const std::string& lookupKey = ItemMapping::IsStar(modId) ? gameKey : modId;
            if (state.GrantedItems.count(lookupKey) == 0) {
                toRemoveGameKeys.push_back(gameKey);
            }
        }
    }
    catch (...) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] Error iterating TMap during enforcement\n"));
        return;
    }

    // Remove non-granted items from TMap (using game-format keys)
    if (!toRemoveGameKeys.empty()) {
        int removed = 0;
        for (const auto& gk : toRemoveGameKeys) {
            try {
                FString key = ToFString(gk);
                tmap->Remove(key);
                ++removed;
            }
            catch (...) {
                // Individual removal failed, continue
            }
        }
        if (removed > 0) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] Enforced: removed {}/{} non-granted items from TMap\n"),
                removed, toRemoveGameKeys.size());
        }
    }

    // Phase 2: Ensure all granted items are in TMap (using game-format keys)
    for (const auto& id : state.GrantedItems) {
        try {
            std::string gameKey = itemMapping.ToGameKey(id);
            FString key = ToFString(gameKey);
            bool* existing = tmap->Find(key);
            if (!existing) {
                tmap->Add(key, false);
            }
        }
        catch (...) {
            // Individual add failed, continue
        }
    }


    // Phase 3: Reusable tetrominos — reset "used" flag
    if (state.ReusableTetrominos) {
        try {
            for (auto& pair : *tmap) {
                if (pair.Value == true) {
                    pair.Value = false;
                }
            }
        }
        catch (...) {
            // Reusable reset failed, non-critical
        }
    }
}

// ============================================================
// RefreshUI
// ============================================================

void InventorySync::RefreshUI()
{
    // TODO: Implement stable UI refresh.
    // The game's ArrangerInfoPanel::UpdateInventory() should be called
    // after the CollectedTetrominos TMap is updated, but the current
    // approach of finding the widget and calling it directly is unstable.
    // Needs further investigation into safe access patterns.
}

// ============================================================
// DumpCollectedTetrominos
// ============================================================

void InventorySync::DumpCollectedTetrominos(ModState& state, const ItemMapping& itemMapping)
{
    if (!state.CurrentProgress) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] No progress object for dump\n"));
        return;
    }

    auto* tmap = GetCollectedTetrominosMap(state.CurrentProgress);
    if (!tmap) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] Cannot access CollectedTetrominos TMap\n"));
        return;
    }

    Output::send<LogLevel::Verbose>(STR("[TalosAP] === CollectedTetrominos TMap ({} entries) ===\n"), tmap->Num());

    try {
        for (auto& pair : *tmap) {
            // Read raw wchar values for hex dump
            const wchar_t* raw = *pair.Key;
            std::wstring hexStr;
            std::wstring readableStr;
            if (raw) {
                for (int ci = 0; raw[ci] != 0; ++ci) {
                    wchar_t wc = raw[ci];
                    // Hex representation
                    wchar_t hexBuf[8];
                    swprintf(hexBuf, 8, L"%02X ", (unsigned int)wc);
                    hexStr += hexBuf;
                    // Readable representation (printable or '.')
                    readableStr += (wc >= 0x20 && wc < 0x7F) ? wc : L'.';
                }
            }
            bool used = pair.Value;
            // Translate game key to mod ID for readability
            std::string narrowKey = FromFString(pair.Key);
            std::string modId = itemMapping.FromGameKey(narrowKey);
            std::wstring modIdW(modId.begin(), modId.end());
            Output::send<LogLevel::Verbose>(STR("[TalosAP]   [{}] \"{}\" ({}) = {}\n"),
                hexStr,
                readableStr,
                modIdW,
                used ? STR("true (used)") : STR("false (unused)"));
        }
    }
    catch (...) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] Error iterating TMap during dump\n"));
    }

    Output::send<LogLevel::Verbose>(STR("[TalosAP] === Granted items ({}) ===\n"), state.GrantedItems.size());
    for (const auto& id : state.GrantedItems) {
        Output::send<LogLevel::Verbose>(STR("[TalosAP]   {}\n"), std::wstring(id.begin(), id.end()));
    }

    Output::send<LogLevel::Verbose>(STR("[TalosAP] === Checked locations ({}) ===\n"), state.CheckedLocations.size());
    for (const auto& id : state.CheckedLocations) {
        Output::send<LogLevel::Verbose>(STR("[TalosAP]   {}\n"), std::wstring(id.begin(), id.end()));
    }
}

// ============================================================
// ResetCachedFunctions — call on level transition
// ============================================================

static UFunction* s_fnBoolSet        = nullptr;
static bool       s_fnPuzzleResolved = false;

void InventorySync::ResetCachedFunctions()
{
    s_fnBoolSet        = nullptr;
    s_fnPuzzleResolved = false;
    s_solvedPuzzleCache.clear();

    Output::send<LogLevel::Verbose>(STR("[TalosAP] PuzzleSolve: Reset cached functions and puzzle cache\n"));
}

// ============================================================
// Static tetromino → puzzle ID mapping (from BotPuzzleDatabase.csv)
// ============================================================

static const std::unordered_map<std::string, std::string>& GetTetrominoToPuzzleMap()
{
    static const std::unordered_map<std::string, std::string> map = {
        // Regular puzzles: tetromino mod-ID → puzzle number
        {"MT1",  "001"},  {"DJ3",  "011"},  {"DJ1",  "107"},  {"DJ2",  "006"},
        {"DZ1",  "007"},  {"ML1",  "012"},  {"DI1",  "005"},  {"ML2",  "013"},
        {"DZ2",  "001a"}, {"DL1",  "008"},  {"MT2",  "015"},  {"DZ3",  "108"},
        {"MT3",  "020"},  {"NL1",  "017"},  {"MZ1",  "202b"}, {"MT4",  "202c"},
        {"MZ2",  "202d"}, {"MT5",  "202f"}, {"DT2",  "207"},  {"DL2",  "204"},
        {"DT1",  "202e"}, {"NZ1",  "244"},  {"DI2",  "201"},  {"NL3",  "218"},
        {"NZ2",  "303"},  {"NL2",  "210"},  {"DZ4",  "111"},  {"NO1",  "220"},
        {"DL3",  "212"},  {"DT3",  "305"},  {"NT1",  "211"},  {"NL4",  "209"},
        {"ML3",  "203"},  {"MZ3",  "205"},  {"MS1",  "302"},  {"MT6",  "316"},
        {"MT7",  "319"},  {"MS2",  "213"},  {"MZ4",  "223"},  {"NL5",  "120"},
        {"MT8",  "221"},  {"MT9",  "222"},  {"NT2",  "409"},  {"MJ1",  "300a"},
        {"NL6",  "401"},  {"NL8",  "414"},  {"DJ4",  "322"},  {"DT4",  "321"},
        {"NT4",  "407"},  {"NL7",  "310"},  {"NT3",  "215"},  {"DJ5",  "314"},
        {"NL9",  "239"},  {"NZ3",  "315"},  {"NS1",  "311"},  {"NI1",  "238"},
        {"MT10", "206"},  {"ML4",  "208"},  {"NI2",  "113"},  {"MI1",  "301"},
        {"NJ1",  "118"},  {"NI3",  "224"},  {"MO1",  "402"},  {"NT5",  "114"},
        {"NI4",  "219"},  {"NJ2",  "416"},  {"NZ4",  "312"},  {"NS2",  "417"},
        {"NZ5",  "418"},  {"NO2",  "403"},  {"NT6",  "217"},  {"NT7",  "229"},
        {"NO3",  "225"},  {"NZ6",  "318"},  {"NJ3",  "317"},  {"NT9",  "408"},
        {"NS3",  "405"},  {"NI5",  "313"},  {"NT8",  "216"},  {"NO4",  "232"},
        {"NO5",  "309"},  {"NT10", "404"},  {"NI6",  "328"},  {"NO6",  "234"},
        {"NS4",  "112"},  {"NJ4",  "226"},  {"NT12", "233"},  {"NT11", "227"},
        {"NL10", "308"},  {"NO7",  "230"},
        // Bonus puzzles (E-type)
        {"EL1",  "S119"}, {"ES1",  "S117"}, {"ES3",  "S115"}, {"EL2",  "S214"},
        {"EL3",  "S411"}, {"ES2",  "S306"}, {"EL4",  "S235"}, {"EO1",  "S320"},
        {"ES4",  "S504"},
    };
    return map;
}

// ============================================================
// EnforcePuzzleSolvedState — mark checked puzzle locations as solved for signs
// Uses BoolSet("Puzzle::<id>", true) directly on UTalosProgress.
// ============================================================

void InventorySync::EnforcePuzzleSolvedState(ModState& state, const ItemMapping& /*itemMapping*/)
{
    if (!state.CurrentProgress) return;
    if (!state.APSynced) return;

    // Resolve BoolSet UFunction once (cached, same pattern as DoorUnlockHandler)
    if (!s_fnPuzzleResolved) {
        s_fnPuzzleResolved = true;
        s_fnBoolSet = SEH_StaticFindFunction(STR("/Script/Talos.TalosProgress:BoolSet"));
        Output::send<LogLevel::Verbose>(STR("[TalosAP] PuzzleSolve: Resolved BoolSet={}\n"),
            s_fnBoolSet ? STR("ok") : STR("MISS"));
    }

    if (!s_fnBoolSet) return;

    const auto& puzzleMap = GetTetrominoToPuzzleMap();
    int solvedCount = 0;

    for (const auto& id : state.CheckedLocations) {
        // Skip items already processed this session
        if (s_solvedPuzzleCache.count(id) > 0) continue;

        // Look up the puzzle ID from our static mapping
        auto it = puzzleMap.find(id);
        if (it == puzzleMap.end()) {
            // Not a regular/bonus puzzle location (could be a star "**5", etc.) — skip
            s_solvedPuzzleCache.insert(id);
            continue;
        }

        const std::string& puzzleNum = it->second;
        std::wstring varName = L"Puzzle::" + std::wstring(puzzleNum.begin(), puzzleNum.end());

        // Call BoolSet(varName, true) on the progress object
        struct {
            FString Name;
            bool    bValue;
        } params{};
        params.Name = FString(varName.c_str());
        params.bValue = true;

        if (SEH_ProcessEvent(state.CurrentProgress, s_fnBoolSet, &params)) {
            ++solvedCount;
            Output::send<LogLevel::Verbose>(STR("[TalosAP] PuzzleSolve: {} → {} = true\n"),
                std::wstring(id.begin(), id.end()), varName);
        }

        s_solvedPuzzleCache.insert(id);
    }

    if (solvedCount > 0) {
        Output::send<LogLevel::Verbose>(STR("[TalosAP] PuzzleSolve: Marked {} puzzles as solved this pass\n"), solvedCount);
    }
}

} // namespace TalosAP
