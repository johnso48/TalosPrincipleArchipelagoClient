#include "headers/DebugCommands.h"
#include "headers/InventorySync.h"

#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

#include <vector>
#include <string>
#include <cstdint>
#include <excpt.h>

using namespace RC;
using namespace RC::Unreal;

// ============================================================
// SEH-safe REFLECTION helpers
//
// All property reads go through UE4SS's GetValuePtrByPropertyNameInChain
// which resolves real offsets at runtime via Unreal's reflection system.
// NO hardcoded byte offsets are used — the UE4SS CXX header dump has
// KeepMemoryLayout=0 so its offsets are unreliable.
//
// MSVC C2712: __try cannot coexist with C++ objects that have destructors
// in the same function scope.  GetValuePtrByPropertyNameInChain and
// GetFullName may create internal temporaries (FName, std::wstring).
// Solution: split into two layers:
//   1) A thin SEH wrapper that dereferences a raw void* (no C++ objects).
//   2) A normal C++ function that does the reflection lookup, gets the
//      raw pointer, and passes it to the SEH wrapper for the dereference.
// ============================================================

// ---------- Layer 1: thin SEH wrappers (no C++ objects) ----------

static bool SehDerefBool(const void* ptr, bool& out)
{
    __try { out = *static_cast<const bool*>(ptr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool SehDerefObjPtr(const void* ptr, UObject*& out)
{
    __try { out = *static_cast<UObject* const*>(ptr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { out = nullptr; return false; }
}

static bool SehDerefU8(const void* ptr, uint8_t& out)
{
    __try { out = *static_cast<const uint8_t*>(ptr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool SehDerefDouble(const void* ptr, double& out)
{
    __try { out = *static_cast<const double*>(ptr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool SehDerefU64(const void* ptr, uint64_t& out)
{
    __try { out = *static_cast<const uint64_t*>(ptr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool SehWriteU8(void* ptr, uint8_t val)
{
    __try { *static_cast<volatile uint8_t*>(ptr) = val; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

/// SEH probe: just touch the first byte at `ptr`. Returns false on AV.
static bool SehProbe(const void* ptr)
{
    __try { (void)*static_cast<const volatile uint8_t*>(ptr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}


/// SEH-safe FindFirstOf.  Returns nullptr on AV.
static UObject* SEH_FindFirstOf(const wchar_t* className)
{
    __try { return UObjectGlobals::FindFirstOf(className); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

/// SEH-safe ProcessEvent.  Returns true on success.
static bool SafeProcessEvent(UObject* obj, UFunction* fn, void* params)
{
    __try { obj->ProcessEvent(fn, params); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---------- Layer 2: reflection + SEH (normal C++ with try/catch) ----------

static bool ReflReadBool(UObject* obj, const wchar_t* propName, bool& outVal)
{
    try {
        auto* ptr = obj->GetValuePtrByPropertyNameInChain<bool>(propName);
        if (!ptr) return false;
        return SehDerefBool(ptr, outVal);
    } catch (...) { return false; }
}

static bool ReflReadObjPtr(UObject* obj, const wchar_t* propName, UObject*& outVal)
{
    try {
        auto* ptr = obj->GetValuePtrByPropertyNameInChain<UObject*>(propName);
        if (!ptr) return false;
        return SehDerefObjPtr(ptr, outVal);
    } catch (...) { outVal = nullptr; return false; }
}

static bool ReflReadU8(UObject* obj, const wchar_t* propName, uint8_t& outVal)
{
    try {
        auto* ptr = obj->GetValuePtrByPropertyNameInChain<uint8_t>(propName);
        if (!ptr) return false;
        return SehDerefU8(ptr, outVal);
    } catch (...) { return false; }
}

static bool ReflReadDouble(UObject* obj, const wchar_t* propName, double& outVal)
{
    try {
        auto* ptr = obj->GetValuePtrByPropertyNameInChain<double>(propName);
        if (!ptr) return false;
        return SehDerefDouble(ptr, outVal);
    } catch (...) { return false; }
}

static bool ReflReadRaw8(UObject* obj, const wchar_t* propName, uint64_t& outVal)
{
    try {
        auto* ptr = obj->GetValuePtrByPropertyNameInChain<uint8_t>(propName);
        if (!ptr) return false;
        return SehDerefU64(ptr, outVal);
    } catch (...) { return false; }
}


/// SEH-safe GetFullName.  Returns L"<stale/AV>" on access violation.
/// Uses SehProbe to verify the object is accessible before calling
/// GetFullName() (which creates std::wstring temporaries).
static std::wstring SafeGetFullName(UObject* obj)
{
    if (!obj) return L"<null>";
    if (!SehProbe(obj)) return L"<stale/AV>";
    try {
        return obj->GetFullName();
    } catch (...) {
        return L"<exception>";
    }
}

namespace TalosAP {

void DebugCommands::ProcessPending(ModState& state,
                                   ItemMapping& itemMapping,
                                   VisibilityManager& visibilityManager,
                                   HudNotification* hud)
{
    // F6: inventory dump + save-game state
    if (state.PendingInventoryDump.exchange(false)) {
        Output::send<LogLevel::Verbose>(STR("[TalosAP] === F6 Inventory Dump ===\n"));
        InventorySync::FindProgressObject(state);
        InventorySync::DumpCollectedTetrominos(state, itemMapping);
        visibilityManager.DumpTracked();
        visibilityManager.DumpFenceMap();
        DumpSaveGameState(state);
    }

    // F9: HUD notification test
    if (state.PendingHudTest.exchange(false) && hud) {
        Output::send<LogLevel::Verbose>(STR("[TalosAP] === F9: HUD notification test ===\n"));
        hud->Notify({
            { L"Alice",         HudColors::PLAYER },
            { L" sent you a ",   HudColors::WHITE  },
            { L"Red L",         HudColors::TRAP   },
        });
        hud->Notify({
            { L"Bob",           HudColors::PLAYER },
            { L" sent you a ",   HudColors::WHITE  },
            { L"Golden T",      HudColors::PROGRESSION },
        });
        hud->Notify({
            { L"You found a ",   HudColors::WHITE  },
            { L"Green J",       HudColors::ITEM   },
        });
        hud->NotifySimple(L"AP Connected to server", HudColors::SERVER);
    }
}

// ============================================================
// F6 extension: Dump save-game / progress state for arrangers
//
// For every arranger in the level, queries:
//   1) GetPuzzleId() — the string key used in save data
//   2) IsSolved() — runtime in-memory state
//   3) UPuzzleMemoryFunctions::IsArrangerSolved(ctx, puzzleId, progress)
//   4) UPuzzleMemoryFunctions::IsPuzzleSolved(ctx, puzzleId)
//   5) UTalosProgress::BoolGet(puzzleId + related keys)
//   6) UTalosProgress::GetDebugString() — full save-data dump
// ============================================================

void DebugCommands::DumpSaveGameState(ModState& state)
{
    Output::send<LogLevel::Verbose>(STR("[TalosAP] === F6: Save-Game / Progress State ===\n"));

    // --- Get the progress object ---
    UObject* progress = state.CurrentProgress;
    if (!progress) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] F6-SAVE: No progress object available\n"));
        return;
    }
    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: Progress = {}\n"), SafeGetFullName(progress));

    // --- Get a world context object for PuzzleMemoryFunctions ---
    UObject* worldCtx = SEH_FindFirstOf(STR("PlayerController"));
    if (!worldCtx) {
        worldCtx = SEH_FindFirstOf(STR("TalosGameInstance"));
    }
    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: WorldContext = {}\n"),
        worldCtx ? SafeGetFullName(worldCtx) : STR("<null>"));

    // --- Resolve UFunctions ---
    UFunction* fnGetPuzzleId     = nullptr;
    UFunction* fnIsSolved        = nullptr;
    UFunction* fnIsArrangerSolved = nullptr;  // PuzzleMemoryFunctions::IsArrangerSolved
    UFunction* fnIsPuzzleSolved   = nullptr;  // PuzzleMemoryFunctions::IsPuzzleSolved
    UFunction* fnSetArrangerSolved = nullptr; // PuzzleMemoryFunctions::SetArrangerSolved
    UFunction* fnSetPuzzleSolved   = nullptr; // PuzzleMemoryFunctions::SetPuzzleSolved
    UFunction* fnBoolGet          = nullptr;  // UTalosProgress::BoolGet
    UFunction* fnIsVariableSet    = nullptr;  // UTalosProgress::IsVariableSet
    UFunction* fnGetDebugString   = nullptr;  // UTalosProgress::GetDebugString
    UFunction* fnStringGet        = nullptr;  // UTalosProgress::StringGet
    UFunction* fnIntegerGet       = nullptr;  // UTalosProgress::IntegerGet

    try { fnGetPuzzleId      = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.Arranger:GetPuzzleId")); } catch (...) {}
    if (!fnGetPuzzleId) {
        try { fnGetPuzzleId  = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Angelscript.ScriptArranger:GetPuzzleId")); } catch (...) {}
    }
    try { fnIsSolved         = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.Arranger:IsSolved")); } catch (...) {}
    try { fnIsArrangerSolved = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.PuzzleMemoryFunctions:IsArrangerSolved")); } catch (...) {}
    try { fnIsPuzzleSolved   = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.PuzzleMemoryFunctions:IsPuzzleSolved")); } catch (...) {}
    try { fnSetArrangerSolved = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.PuzzleMemoryFunctions:SetArrangerSolved")); } catch (...) {}
    try { fnSetPuzzleSolved   = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.PuzzleMemoryFunctions:SetPuzzleSolved")); } catch (...) {}
    try { fnBoolGet          = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.TalosProgress:BoolGet")); } catch (...) {}
    try { fnIsVariableSet    = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.TalosProgress:IsVariableSet")); } catch (...) {}
    try { fnGetDebugString   = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.TalosProgress:GetDebugString")); } catch (...) {}
    try { fnStringGet        = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.TalosProgress:StringGet")); } catch (...) {}
    try { fnIntegerGet       = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.TalosProgress:IntegerGet")); } catch (...) {}

    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: UFunctions: GetPuzzleId={} IsSolved={} IsArrangerSolved={} IsPuzzleSolved={} SetArrangerSolved={} SetPuzzleSolved={}\n"),
        fnGetPuzzleId      ? STR("ok") : STR("MISS"),
        fnIsSolved         ? STR("ok") : STR("MISS"),
        fnIsArrangerSolved ? STR("ok") : STR("MISS"),
        fnIsPuzzleSolved   ? STR("ok") : STR("MISS"),
        fnSetArrangerSolved? STR("ok") : STR("MISS"),
        fnSetPuzzleSolved  ? STR("ok") : STR("MISS"));
    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: UFunctions: BoolGet={} IsVariableSet={} GetDebugString={} StringGet={} IntegerGet={}\n"),
        fnBoolGet        ? STR("ok") : STR("MISS"),
        fnIsVariableSet  ? STR("ok") : STR("MISS"),
        fnGetDebugString ? STR("ok") : STR("MISS"),
        fnStringGet      ? STR("ok") : STR("MISS"),
        fnIntegerGet     ? STR("ok") : STR("MISS"));

    // --- Get the PuzzleMemoryFunctions CDO (it's a BlueprintFunctionLibrary) ---
    UObject* pmfCDO = nullptr;
    try { pmfCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Talos.Default__PuzzleMemoryFunctions")); } catch (...) {}
    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: PuzzleMemoryFunctions CDO = {}\n"),
        pmfCDO ? SafeGetFullName(pmfCDO) : STR("<null>"));

    // --- Enumerate all arrangers in the level ---
    std::vector<UObject*> arrangers;
    try { UObjectGlobals::FindAllOf(STR("BP_ArrangerDoor_C"), arrangers); } catch (...) {}

    // Also find all ScriptArranger (non-door arrangers)
    std::vector<UObject*> allArrangers;
    try { UObjectGlobals::FindAllOf(STR("ScriptArranger"), allArrangers); } catch (...) {}
    // Merge, avoiding duplicates
    for (auto* a : allArrangers) {
        bool found = false;
        for (auto* e : arrangers) { if (e == a) { found = true; break; } }
        if (!found) arrangers.push_back(a);
    }

    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: Found {} arranger(s) in level\n"), arrangers.size());

    for (auto* arr : arrangers) {
        if (!arr) continue;
        try {
            std::wstring puzzleIdStr = L"<unknown>";
            if (fnGetPuzzleId) {
                try {
                    struct { FString ReturnValue; } params{};
                    arr->ProcessEvent(fnGetPuzzleId, &params);
                    const wchar_t* raw = *params.ReturnValue;
                    if (raw && raw[0] != L'\0') puzzleIdStr = raw;
                } catch (...) {}
            }

            // Get the actor class name
            std::wstring clsName = L"<unknown>";
            try {
                auto* cls = arr->GetClassPrivate();
                if (cls) clsName = cls->GetName();
            } catch (...) {}

            Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: --- Arranger '{}' (class={}) ---\n"), puzzleIdStr, clsName);

            // 1) IsSolved() — runtime state on the actor
            if (fnIsSolved) {
                try {
                    struct { bool ReturnValue; } p{};
                    arr->ProcessEvent(fnIsSolved, &p);
                    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE:   IsSolved() = {}\n"),
                        p.ReturnValue ? STR("TRUE") : STR("false"));
                } catch (...) {
                    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE:   IsSolved() = <exception>\n"));
                }
            }

            // 2) PuzzleMemoryFunctions::IsArrangerSolved(worldCtx, puzzleId, progress)
            if (fnIsArrangerSolved && pmfCDO && worldCtx) {
                try {
                    struct {
                        UObject* Context;
                        FString ArrangerPuzzleId;
                        UObject* UsedProgress;
                        bool ReturnValue;
                    } p{};
                    p.Context = worldCtx;
                    p.ArrangerPuzzleId = FString(puzzleIdStr.c_str());
                    p.UsedProgress = progress;
                    p.ReturnValue = false;
                    pmfCDO->ProcessEvent(fnIsArrangerSolved, &p);
                    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE:   IsArrangerSolved('{}') = {}\n"),
                        puzzleIdStr, p.ReturnValue ? STR("TRUE") : STR("false"));
                } catch (...) {
                    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE:   IsArrangerSolved() = <exception>\n"));
                }
            }

            // 3) PuzzleMemoryFunctions::IsPuzzleSolved(worldCtx, puzzleId)
            if (fnIsPuzzleSolved && pmfCDO && worldCtx) {
                try {
                    struct {
                        UObject* Context;
                        FString PuzzleId;
                        bool ReturnValue;
                    } p{};
                    p.Context = worldCtx;
                    p.PuzzleId = FString(puzzleIdStr.c_str());
                    p.ReturnValue = false;
                    pmfCDO->ProcessEvent(fnIsPuzzleSolved, &p);
                    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE:   IsPuzzleSolved('{}') = {}\n"),
                        puzzleIdStr, p.ReturnValue ? STR("TRUE") : STR("false"));
                } catch (...) {
                    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE:   IsPuzzleSolved() = <exception>\n"));
                }
            }

            // 4) UTalosProgress::BoolGet for various puzzle-related variable names
            if (fnBoolGet) {
                // Try common naming patterns the game might use
                std::vector<std::wstring> varNames = {
                    puzzleIdStr,                                    // e.g. "Cloud_1_01_Puzzle01"
                    puzzleIdStr + L"_Solved",
                    puzzleIdStr + L"_solved",
                    L"Arranger_" + puzzleIdStr,
                    L"Arranger_" + puzzleIdStr + L"_Solved",
                    L"Puzzle_" + puzzleIdStr,
                    L"Puzzle_" + puzzleIdStr + L"_Solved",
                };
                for (const auto& varName : varNames) {
                    try {
                        struct {
                            FString Name;
                            bool bDefault;
                            bool ReturnValue;
                        } p{};
                        p.Name = FString(varName.c_str());
                        p.bDefault = false;
                        p.ReturnValue = false;
                        progress->ProcessEvent(fnBoolGet, &p);
                        // Also check IsVariableSet
                        bool isSet = false;
                        if (fnIsVariableSet) {
                            struct {
                                FString Name;
                                bool ReturnValue;
                            } ivp{};
                            ivp.Name = FString(varName.c_str());
                            ivp.ReturnValue = false;
                            progress->ProcessEvent(fnIsVariableSet, &ivp);
                            isSet = ivp.ReturnValue;
                        }
                        if (isSet || p.ReturnValue) {
                            Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE:   BoolGet('{}') = {} (isSet={})\n"),
                                varName,
                                p.ReturnValue ? STR("TRUE") : STR("false"),
                                isSet ? STR("YES") : STR("no"));
                        }
                    } catch (...) {}
                }
            }
        } catch (...) {
            Output::send<LogLevel::Warning>(STR("[TalosAP] F6-SAVE:   Exception processing arranger\n"));
        }
    }

    // --- Dump the SeenArrangers TArray<FString> ---
    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: === SeenArrangers ===\n"));
    try {
        auto* seenPtr = progress->GetValuePtrByPropertyNameInChain<uint8_t>(STR("SeenArrangers"));
        if (seenPtr) {
            // TArray<FString>: Data* at +0, Num at +8
            FString** dataPtr = reinterpret_cast<FString**>(seenPtr);
            int32_t num = *reinterpret_cast<int32_t*>(seenPtr + sizeof(void*));
            FString* data = *dataPtr;
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: SeenArrangers count = {}\n"), num);
            if (data && num > 0 && num < 1000) {
                for (int32_t i = 0; i < num; i++) {
                    try {
                        const wchar_t* s = *data[i];
                        if (s) {
                            Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE:   [{}] '{}'\n"), i, s);
                        }
                    } catch (...) {}
                }
            }
        } else {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: SeenArrangers property not found\n"));
        }
    } catch (...) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] F6-SAVE: Exception reading SeenArrangers\n"));
    }

    // --- Dump UTalosProgress::GetDebugString() ---
    if (fnGetDebugString) {
        Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: === TalosProgress::GetDebugString() ===\n"));
        try {
            struct { FString ReturnValue; } p{};
            progress->ProcessEvent(fnGetDebugString, &p);
            const wchar_t* dbgStr = *p.ReturnValue;
            if (dbgStr && dbgStr[0] != L'\0') {
                // Split by newlines for readability in the log
                std::wstring full(dbgStr);
                size_t pos = 0;
                while (pos < full.size()) {
                    size_t nl = full.find(L'\n', pos);
                    if (nl == std::wstring::npos) nl = full.size();
                    std::wstring line = full.substr(pos, nl - pos);
                    if (!line.empty()) {
                        Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE:   {}\n"), line);
                    }
                    pos = nl + 1;
                }
            } else {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE:   <empty>\n"));
            }
        } catch (...) {
            Output::send<LogLevel::Warning>(STR("[TalosAP] F6-SAVE:   Exception calling GetDebugString\n"));
        }
    }

    // --- Dump a few GameMemory variables related to arrangers/doors ---
    // We don't know the exact variable names, so also try reading the
    // GameMemory.SetBooleanVariables TArray<FString> to enumerate them.
    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: === GameMemory SetBooleanVariables ===\n"));
    try {
        // GameMemory is at offset 0x0188 in UTalosProgress
        // SetBooleanVariables (TArray<FString>) is at GameMemory+0x0058 = Progress+0x01E0
        uint8_t* progBase = reinterpret_cast<uint8_t*>(progress);
        uint8_t* setBoolVarsRaw = progBase + 0x01E0;
        if (SehProbe(setBoolVarsRaw)) {
            FString** dataPtr = reinterpret_cast<FString**>(setBoolVarsRaw);
            int32_t num = *reinterpret_cast<int32_t*>(setBoolVarsRaw + sizeof(void*));
            FString* data = *dataPtr;
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: SetBooleanVariables count = {}\n"), num);
            if (data && num > 0 && num < 10000) {
                for (int32_t i = 0; i < num; i++) {
                    try {
                        const wchar_t* s = *data[i];
                        if (s) {
                            Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE:   [{}] '{}'\n"), i, s);
                        }
                    } catch (...) {}
                }
            }
        } else {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: Cannot probe GameMemory.SetBooleanVariables\n"));
        }
    } catch (...) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] F6-SAVE: Exception reading SetBooleanVariables\n"));
    }

    Output::send<LogLevel::Verbose>(STR("[TalosAP] === End F6 Save-Game State (part 1) ===\n"));

    // =================================================================
    // NEW: Dump CollectedTetrominos TMap + mechanic unlock verification
    //
    // UTalosProgress layout:
    //   0x02D8  TMap<FString, bool>  CollectedTetrominos   (size 0x50)
    //
    // FTetrominoInstanceInfo { uint8 Type, uint8 Shape, int32 Number }
    // ETetrominoPieceType:  None=0 Door=1 Mechanic=2 Nexus=4 Secret=8 ...
    // ETetrominoPieceShape: None=0 I=1 J=2 L=4 O=8 S=16 T=32 Z=64
    // EPuzzleMechanic:      None=0 Time=1 Cube=2 Fan=4 Rod=8 Platform=16
    // =================================================================

    // --- Dump CollectedTetrominos TMap via raw memory ---
    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: === CollectedTetrominos (raw TMap) ===\n"));
    try {
        uint8_t* progBase = reinterpret_cast<uint8_t*>(progress);
        // TMap<FString,bool> at offset 0x02D8 in UTalosProgress
        // TMap layout: Elements (TSparseArray) at +0x00
        //   TSparseArray: Data* at +0x00, NumElements at +0x08(or via BitArray)
        // For a simpler approach, use CountCollectedTetrominos + GetAllCollectedTetrominos.
        // But first, try the raw pointer as a sanity check.
        uint8_t* tmapBase = progBase + 0x02D8;
        if (SehProbe(tmapBase)) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: CollectedTetrominos TMap base probed OK at Progress+0x02D8\n"));
        } else {
            Output::send<LogLevel::Warning>(STR("[TalosAP] F6-SAVE: CollectedTetrominos TMap base not accessible\n"));
        }
    } catch (...) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] F6-SAVE: Exception probing CollectedTetrominos\n"));
    }

    // --- Resolve additional UFunctions for tetromino/mechanic queries ---
    UFunction* fnCountCollected       = nullptr;  // UTalosProgress::CountCollectedTetrominos
    UFunction* fnGetAllCollected      = nullptr;  // PuzzleMemoryFunctions::GetAllCollectedTetrominos
    UFunction* fnGetAllCollectedType  = nullptr;  // PuzzleMemoryFunctions::GetAllCollectedTetrominosOfType
    UFunction* fnIsMechUnlocked       = nullptr;  // PuzzleMemoryFunctions::IsMechanicUnlocked
    UFunction* fnAreMechsUnlocked     = nullptr;  // PuzzleMemoryFunctions::AreMechanicsUnlocked
    UFunction* fnMarkTetrominoCollected = nullptr; // PuzzleMemoryFunctions::MarkTetrominoCollected
    UFunction* fnIsTetrominoCollected = nullptr;   // PuzzleMemoryFunctions::IsTetrominoCollected
    UFunction* fnCountAllOfType       = nullptr;   // PuzzleMemoryFunctions::CountAllCollectedTetrominosOfType

    try { fnCountCollected       = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.TalosProgress:CountCollectedTetrominos")); } catch (...) {}
    try { fnGetAllCollected      = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.PuzzleMemoryFunctions:GetAllCollectedTetrominos")); } catch (...) {}
    try { fnGetAllCollectedType  = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.PuzzleMemoryFunctions:GetAllCollectedTetrominosOfType")); } catch (...) {}
    try { fnIsMechUnlocked       = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.PuzzleMemoryFunctions:IsMechanicUnlocked")); } catch (...) {}
    try { fnAreMechsUnlocked     = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.PuzzleMemoryFunctions:AreMechanicsUnlocked")); } catch (...) {}
    try { fnMarkTetrominoCollected = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.PuzzleMemoryFunctions:MarkTetrominoCollected")); } catch (...) {}
    try { fnIsTetrominoCollected = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.PuzzleMemoryFunctions:IsTetrominoCollected")); } catch (...) {}
    try { fnCountAllOfType       = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.PuzzleMemoryFunctions:CountAllCollectedTetrominosOfType")); } catch (...) {}

    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: Tetromino UFunctions: CountCollected={} GetAll={} GetAllOfType={} IsMechUnlocked={} AreMechsUnlocked={}\n"),
        fnCountCollected      ? STR("ok") : STR("MISS"),
        fnGetAllCollected     ? STR("ok") : STR("MISS"),
        fnGetAllCollectedType ? STR("ok") : STR("MISS"),
        fnIsMechUnlocked      ? STR("ok") : STR("MISS"),
        fnAreMechsUnlocked    ? STR("ok") : STR("MISS"));
    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: MarkTetrominoCollected={} IsTetrominoCollected={} CountAllOfType={}\n"),
        fnMarkTetrominoCollected ? STR("ok") : STR("MISS"),
        fnIsTetrominoCollected   ? STR("ok") : STR("MISS"),
        fnCountAllOfType         ? STR("ok") : STR("MISS"));

    UObject* pmfCDO2 = nullptr;
    try { pmfCDO2 = UObjectGlobals::StaticFindObject<UObject*>(
        nullptr, nullptr, STR("/Script/Talos.Default__PuzzleMemoryFunctions")); } catch (...) {}

    // --- CountCollectedTetrominos (parameterless on UTalosProgress) ---
    if (fnCountCollected) {
        try {
            struct { int32_t ReturnValue; } p{};
            progress->ProcessEvent(fnCountCollected, &p);
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: CountCollectedTetrominos() = {}\n"), p.ReturnValue);
        } catch (...) {
            Output::send<LogLevel::Warning>(STR("[TalosAP] F6-SAVE: CountCollectedTetrominos() threw\n"));
        }
    }

    // --- GetAllCollectedTetrominos (on PuzzleMemoryFunctions CDO) ---
    // Returns TArray<FTetrominoInstanceInfo>
    // FTetrominoInstanceInfo { uint8 Type (0x00), uint8 Shape (0x01), padding, int32 Number (0x04) } size=0x08
    if (fnGetAllCollected && pmfCDO2 && worldCtx) {
        Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: === GetAllCollectedTetrominos ===\n"));
        try {
            struct {
                UObject*  Context;
                UObject*  UsedProgress;
                // Return: TArray<FTetrominoInstanceInfo> — 16 bytes (ptr + count + capacity)
                uint8_t*  RetData;
                int32_t   RetNum;
                int32_t   RetMax;
            } p{};
            p.Context = worldCtx;
            p.UsedProgress = progress;
            p.RetData = nullptr;
            p.RetNum = 0;
            p.RetMax = 0;
            pmfCDO2->ProcessEvent(fnGetAllCollected, &p);
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: GetAllCollectedTetrominos count = {}\n"), p.RetNum);
            if (p.RetData && p.RetNum > 0 && p.RetNum < 10000) {
                // Each element is 8 bytes: { uint8 Type, uint8 Shape, pad[2], int32 Number }
                for (int32_t i = 0; i < p.RetNum; i++) {
                    uint8_t* elem = p.RetData + (i * 8);
                    if (!SehProbe(elem)) break;
                    uint8_t type   = elem[0];
                    uint8_t shape  = elem[1];
                    int32_t number = *reinterpret_cast<int32_t*>(elem + 4);

                    // Decode type name
                    const wchar_t* typeName = STR("?");
                    switch (type) {
                        case 0: typeName = STR("None"); break;
                        case 1: typeName = STR("Door"); break;
                        case 2: typeName = STR("Mechanic"); break;
                        case 4: typeName = STR("Nexus"); break;
                        case 8: typeName = STR("Secret"); break;
                        case 16: typeName = STR("AltEnding"); break;
                        case 32: typeName = STR("Arcade"); break;
                        case 64: typeName = STR("Help"); break;
                    }
                    // Decode shape name
                    const wchar_t* shapeName = STR("?");
                    switch (shape) {
                        case 0: shapeName = STR("None"); break;
                        case 1: shapeName = STR("I"); break;
                        case 2: shapeName = STR("J"); break;
                        case 4: shapeName = STR("L"); break;
                        case 8: shapeName = STR("O"); break;
                        case 16: shapeName = STR("S"); break;
                        case 32: shapeName = STR("T"); break;
                        case 64: shapeName = STR("Z"); break;
                    }

                    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE:   [{}] Type={}({}) Shape={}({}) Number={}\n"),
                        i, type, typeName, shape, shapeName, number);
                }
            }
        } catch (...) {
            Output::send<LogLevel::Warning>(STR("[TalosAP] F6-SAVE: GetAllCollectedTetrominos threw\n"));
        }
    }

    // --- CountAllCollectedTetrominosOfType for Mechanic (type=2) ---
    if (fnCountAllOfType && pmfCDO2 && worldCtx) {
        try {
            struct {
                UObject*  Context;
                uint8_t   TetrominoType;  // ETetrominoPieceType = 2 for Mechanic
                uint8_t   _pad[7];
                UObject*  UsedProgress;
                int32_t   ReturnValue;
            } p{};
            p.Context = worldCtx;
            p.TetrominoType = 2;  // Mechanic
            p.UsedProgress = progress;
            p.ReturnValue = 0;
            pmfCDO2->ProcessEvent(fnCountAllOfType, &p);
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: CountAllCollectedTetrominosOfType(Mechanic) = {}\n"), p.ReturnValue);
        } catch (...) {
            Output::send<LogLevel::Warning>(STR("[TalosAP] F6-SAVE: CountAllCollectedTetrominosOfType(Mechanic) threw\n"));
        }
    }

    // --- IsMechanicUnlocked for each EPuzzleMechanic ---
    if (fnIsMechUnlocked && pmfCDO2 && worldCtx) {
        Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: === IsMechanicUnlocked checks ===\n"));
        struct MechCheck { uint8_t enumVal; const wchar_t* name; };
        const MechCheck mechs[] = {
            { 1,  L"Time" },
            { 2,  L"Cube" },
            { 4,  L"Fan" },
            { 8,  L"Rod" },
            { 16, L"Platform/Shield" },
        };
        for (const auto& m : mechs) {
            try {
                // IsMechanicUnlocked(const UObject* Context, EPuzzleMechanic Mechanic, UTalosProgress* UsedProgress)
                // EPuzzleMechanic is an enum class with underlying uint8
                // Try several param layout guesses since padding is uncertain
                struct {
                    UObject*  Context;
                    uint8_t   Mechanic;
                    uint8_t   _pad[7];
                    UObject*  UsedProgress;
                    bool      ReturnValue;
                } p{};
                p.Context = worldCtx;
                p.Mechanic = m.enumVal;
                p.UsedProgress = progress;
                p.ReturnValue = false;
                pmfCDO2->ProcessEvent(fnIsMechUnlocked, &p);
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE:   IsMechanicUnlocked({}={}) = {}\n"),
                    m.name, m.enumVal,
                    p.ReturnValue ? STR("TRUE") : STR("false"));
            } catch (...) {
                Output::send<LogLevel::Warning>(STR("[TalosAP] F6-SAVE:   IsMechanicUnlocked({}) threw\n"), m.name);
            }
        }
    }

    // --- AreMechanicsUnlocked with all bits set (0x1F = Time|Cube|Fan|Rod|Platform) ---
    if (fnAreMechsUnlocked && pmfCDO2 && worldCtx) {
        try {
            struct {
                UObject*  Context;
                uint8_t   Mechanics;  // bitmask
                bool      ReturnValue;
            } p{};
            p.Context = worldCtx;
            p.Mechanics = 0x1F;  // all 5
            p.ReturnValue = false;
            pmfCDO2->ProcessEvent(fnAreMechsUnlocked, &p);
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: AreMechanicsUnlocked(0x1F=all) = {}\n"),
                p.ReturnValue ? STR("TRUE") : STR("false"));
        } catch (...) {
            Output::send<LogLevel::Warning>(STR("[TalosAP] F6-SAVE: AreMechanicsUnlocked threw\n"));
        }
    }

    // --- Dump the bHasAscended flag ---
    {
        bool bAscended = false;
        if (ReflReadBool(progress, STR("bHasAscended"), bAscended)) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: bHasAscended = {}\n"),
                bAscended ? STR("TRUE") : STR("false"));
        }
    }

    // --- Dump the bCheated flag ---
    {
        bool bCheated = false;
        if (ReflReadBool(progress, STR("bCheated"), bCheated)) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-SAVE: bCheated = {}\n"),
                bCheated ? STR("TRUE") : STR("false"));
        }
    }

    Output::send<LogLevel::Verbose>(STR("[TalosAP] === End F6 Save-Game State (part 2) ===\n"));

    // =================================================================
    // WORLD CONTEXT DUMP
    //
    // IsMechanicUnlocked takes a world context object.  It may use the
    // context to resolve ATalosWorldSettings, UTalosGameInstance,
    // UTalosSaveGame, or a different UTalosProgress than what we hold.
    // This section dumps everything reachable from the context chain.
    // =================================================================
    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD: === World Context Dump ===\n"));

    // --- 1) All ATalosWorldSettings instances + bAllowAllMechanics ---
    {
        std::vector<UObject*> allWS;
        try { UObjectGlobals::FindAllOf(STR("TalosWorldSettings"), allWS); } catch (...) {}
        Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD: ATalosWorldSettings instances found: {}\n"), allWS.size());
        for (size_t idx = 0; idx < allWS.size(); idx++) {
            auto* ws = allWS[idx];
            if (!ws) continue;
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD:   [{}] {}\n"), idx, SafeGetFullName(ws));
            bool bAllowAll = false;
            if (ReflReadBool(ws, STR("bAllowAllMechanics"), bAllowAll)) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD:       bAllowAllMechanics = {}\n"),
                    bAllowAll ? STR("TRUE") : STR("false"));
            } else {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD:       bAllowAllMechanics = <not found>\n"));
            }
        }
    }

    // --- 2) Resolve TalosWorldSettings via GetCurrent / GetGlobal ---
    {
        UFunction* fnWSGetCurrent = nullptr;
        UFunction* fnWSGetGlobal = nullptr;
        UFunction* fnWSGetLocal = nullptr;
        try { fnWSGetCurrent = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, STR("/Script/Talos.TalosWorldSettings:GetCurrent")); } catch (...) {}
        try { fnWSGetGlobal = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, STR("/Script/Talos.TalosWorldSettings:GetGlobal")); } catch (...) {}
        try { fnWSGetLocal = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, STR("/Script/Talos.TalosWorldSettings:GetLocalForObject")); } catch (...) {}

        // Need the CDO to call static methods on
        UObject* wsCDO = nullptr;
        try { wsCDO = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Talos.Default__TalosWorldSettings")); } catch (...) {}

        Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD: WS CDO={} GetCurrent={} GetGlobal={} GetLocal={}\n"),
            wsCDO ? STR("ok") : STR("null"),
            fnWSGetCurrent ? STR("ok") : STR("MISS"),
            fnWSGetGlobal ? STR("ok") : STR("MISS"),
            fnWSGetLocal ? STR("ok") : STR("MISS"));

        auto callWSFunc = [&](UFunction* fn, const wchar_t* label) {
            if (!fn || !wsCDO || !worldCtx) return;
            try {
                struct { UObject* Context; UObject* ReturnValue; } p{};
                p.Context = worldCtx;
                p.ReturnValue = nullptr;
                wsCDO->ProcessEvent(fn, &p);
                if (p.ReturnValue) {
                    bool bAllow = false;
                    ReflReadBool(p.ReturnValue, STR("bAllowAllMechanics"), bAllow);
                    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD: {}({}) = {} bAllowAllMechanics={}\n"),
                        label, SafeGetFullName(worldCtx),
                        SafeGetFullName(p.ReturnValue),
                        bAllow ? STR("TRUE") : STR("false"));
                } else {
                    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD: {}({}) = <null>\n"),
                        label, SafeGetFullName(worldCtx));
                }
            } catch (...) {
                Output::send<LogLevel::Warning>(STR("[TalosAP] F6-WORLD: {}() threw\n"), label);
            }
        };
        callWSFunc(fnWSGetCurrent, STR("GetCurrent"));
        callWSFunc(fnWSGetGlobal, STR("GetGlobal"));
        callWSFunc(fnWSGetLocal, STR("GetLocalForObject"));
    }

    // --- 3) UserCvars::AllowAllPuzzleMechanics ---
    {
        UObject* userCvars = SEH_FindFirstOf(STR("UserCvars"));
        if (userCvars) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD: UserCvars = {}\n"), SafeGetFullName(userCvars));
            bool bAllow = false;
            if (ReflReadBool(userCvars, STR("AllowAllPuzzleMechanics"), bAllow)) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD: UserCvars::AllowAllPuzzleMechanics = {}\n"),
                    bAllow ? STR("TRUE") : STR("false"));
            } else {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD: UserCvars::AllowAllPuzzleMechanics = <not found via reflection>\n"));
            }
        } else {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD: UserCvars not found\n"));
        }
    }

    // --- 4) TalosGameInstance → SaveGameInstance → Progress chain ---
    //     Compare the game-resolved progress with our state.CurrentProgress
    {
        UObject* gameInst = SEH_FindFirstOf(STR("TalosGameInstance"));
        Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD: TalosGameInstance = {}\n"),
            gameInst ? SafeGetFullName(gameInst) : STR("<not found>"));

        if (gameInst) {
            // Read SaveGameInstance (offset 0x01D8 in UTalosGameInstance)
            UObject* saveGame = nullptr;
            if (ReflReadObjPtr(gameInst, STR("SaveGameInstance"), saveGame)) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD: TalosGameInstance.SaveGameInstance = {}\n"),
                    saveGame ? SafeGetFullName(saveGame) : STR("<null>"));
            } else {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD: TalosGameInstance.SaveGameInstance = <not found>\n"));
            }

            // Also try UTalosSaveGame::Get(context) static call
            UFunction* fnSGGet = nullptr;
            UObject* sgCDO = nullptr;
            try { fnSGGet = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/Talos.TalosSaveGame:Get")); } catch (...) {}
            try { sgCDO = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, STR("/Script/Talos.Default__TalosSaveGame")); } catch (...) {}

            if (fnSGGet && sgCDO && worldCtx) {
                try {
                    struct { UObject* Context; UObject* ReturnValue; } p{};
                    p.Context = worldCtx;
                    p.ReturnValue = nullptr;
                    sgCDO->ProcessEvent(fnSGGet, &p);
                    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD: TalosSaveGame::Get(ctx) = {}\n"),
                        p.ReturnValue ? SafeGetFullName(p.ReturnValue) : STR("<null>"));

                    if (p.ReturnValue) {
                        // Call GetProgress(0) to get slot 0 progress
                        UFunction* fnGetProgress = nullptr;
                        try { fnGetProgress = UObjectGlobals::StaticFindObject<UFunction*>(
                            nullptr, nullptr, STR("/Script/Talos.TalosSaveGame:GetProgress")); } catch (...) {}
                        if (fnGetProgress) {
                            for (int slot = 0; slot < 4; slot++) {
                                try {
                                    struct { int32_t Slot; UObject* ReturnValue; } gp{};
                                    gp.Slot = slot;
                                    gp.ReturnValue = nullptr;
                                    p.ReturnValue->ProcessEvent(fnGetProgress, &gp);
                                    if (gp.ReturnValue) {
                                        bool isSame = (gp.ReturnValue == progress);
                                        Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD:   GetProgress({}) = {} {}\n"),
                                            slot, SafeGetFullName(gp.ReturnValue),
                                            isSame ? STR("<<< MATCHES state.CurrentProgress") : STR(""));
                                    }
                                } catch (...) {}
                            }
                        }

                        // Also dump ProgressHistory TArray count
                        try {
                            uint8_t* sgBase = reinterpret_cast<uint8_t*>(p.ReturnValue);
                            // ProgressHistory at 0x0028 (TArray<UTalosProgress*>)
                            if (SehProbe(sgBase + 0x0028)) {
                                int32_t histCount = *reinterpret_cast<int32_t*>(sgBase + 0x0028 + sizeof(void*));
                                Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD: TalosSaveGame.ProgressHistory count = {}\n"), histCount);
                            }
                        } catch (...) {}
                    }
                } catch (...) {
                    Output::send<LogLevel::Warning>(STR("[TalosAP] F6-WORLD: TalosSaveGame::Get() threw\n"));
                }
            }
        }
    }

    // --- 5) Resolve progress via UTalosProgress::Get (same path as our FindProgressObject) ---
    //     Verify it matches state.CurrentProgress
    {
        UObject* progCDO = nullptr;
        UFunction* fnProgGet = nullptr;
        try { progCDO = UObjectGlobals::StaticFindObject<UObject*>(
            nullptr, nullptr, STR("/Script/Talos.Default__TalosProgress")); } catch (...) {}
        try { fnProgGet = UObjectGlobals::StaticFindObject<UFunction*>(
            nullptr, nullptr, STR("/Script/Talos.TalosProgress:Get")); } catch (...) {}
        if (progCDO && fnProgGet && worldCtx) {
            try {
                struct { UObject* Context; UObject* ReturnValue; } p{};
                p.Context = worldCtx;
                p.ReturnValue = nullptr;
                progCDO->ProcessEvent(fnProgGet, &p);
                bool match = (p.ReturnValue == progress);
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD: TalosProgress::Get(ctx) = {} {}\n"),
                    p.ReturnValue ? SafeGetFullName(p.ReturnValue) : STR("<null>"),
                    match ? STR("<<< MATCHES state.CurrentProgress") : STR("!!! DIFFERENT from state.CurrentProgress !!!"));
                if (p.ReturnValue && !match) {
                    Output::send<LogLevel::Warning>(STR("[TalosAP] F6-WORLD: *** PROGRESS MISMATCH *** Our ptr=0x{:X} Game ptr=0x{:X}\n"),
                        reinterpret_cast<uintptr_t>(progress), reinterpret_cast<uintptr_t>(p.ReturnValue));
                }
            } catch (...) {}
        }
    }

    // --- 6) Dump the world context object itself (PlayerController) ---
    //     IsMechanicUnlocked may access properties on the world context
    {
        Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD: WorldContext used = {}\n"),
            worldCtx ? SafeGetFullName(worldCtx) : STR("<null>"));
    }

    Output::send<LogLevel::Verbose>(STR("[TalosAP] F6-WORLD: === End World Context Dump ===\n"));
}

} // namespace TalosAP
