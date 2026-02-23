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

using namespace RC;
using namespace RC::Unreal;

namespace TalosAP {

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
// FindProgressObject
// ============================================================

void InventorySync::FindProgressObject(ModState& state, bool /*forceRefresh*/)
{
    // Always re-acquire the progress object from scratch.
    // Cached UObject* pointers can become stale at any time due to
    // Unreal GC, and accessing a stale pointer is an access violation
    // (SEH) that try/catch cannot intercept.
    state.CurrentProgress = nullptr;

    try {
        auto* cdo = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Talos.Default__TalosProgress"));

        UObject* worldCtx = nullptr;

        // Try PlayerController as world context
        worldCtx = UObjectGlobals::FindFirstOf(STR("PlayerController"));
        if (!worldCtx) {
            // Fallback: GameInstance
            worldCtx = UObjectGlobals::FindFirstOf(STR("TalosGameInstance"));
        }

        if (cdo && worldCtx) {
            // Call UTalosProgress::Get(WorldContextObject)
            auto* getFunc = cdo->GetFunctionByNameInChain(STR("Get"));
            if (getFunc) {
                struct {
                    UObject* WorldContextObject;
                    UObject* ReturnValue;
                } params{};
                params.WorldContextObject = worldCtx;
                params.ReturnValue = nullptr;

                cdo->ProcessEvent(getFunc, &params);

                if (params.ReturnValue) {
                    // Verify we can read the TMap
                    auto* tmap = GetCollectedTetrominosMap(params.ReturnValue);
                    if (tmap) {
                        state.CurrentProgress = params.ReturnValue;
                        return;
                    }
                }
            }
        }
    }
    catch (...) {
        // Any failure in this process results in not having a progress object.
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

} // namespace TalosAP
