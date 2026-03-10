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

/// SEH probe: just touch the first byte at `ptr`. Returns false on AV.
static bool SehProbe(const void* ptr)
{
    __try { (void)*static_cast<const volatile uint8_t*>(ptr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
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
    // F7: open all door-type arrangers
    if (state.PendingOpenDoorArrangers.exchange(false)) {
        OpenAllDoorArrangers(state, hud);
    }

    // F6: inventory dump
    if (state.PendingInventoryDump.exchange(false)) {
        Output::send<LogLevel::Verbose>(STR("[TalosAP] === F6 Inventory Dump ===\n"));
        InventorySync::FindProgressObject(state);
        InventorySync::DumpCollectedTetrominos(state, itemMapping);
        visibilityManager.DumpTracked();
        visibilityManager.DumpFenceMap();
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
// F7: Open All Door-Type Arrangers
//
// Strategy: bypass SolveArranger entirely.  SolveArranger fires
// OnArrangerSolved which triggers BP/AngelScript event chains,
// timelines, delegates, and audio — leaving objects in a state
// that crashes during GC on level transition.
//
// Instead we discover the physical door actor linked to each
// arranger (via the ArrangerOpensDoorScript) and command it to
// open directly.  The door can be any actor type — LockedGate,
// LoweringFence, etc. — so we resolve the class from the script's
// DoorInfo.EntityPointer metadata and call the right function.
//
// Persistence (marking the arranger solved in GameMemory) is NOT
// handled here; the gate will re-close on level reload.
// ============================================================

void DebugCommands::OpenAllDoorArrangers(ModState& state, HudNotification* hud)
{
    Output::send<LogLevel::Verbose>(STR("[TalosAP] === F7: Open All Door Arrangers ===\n"));

    // --- Resolve UFunction pointers for known openable types ---
    UFunction* fnFenceOpen        = nullptr;
    UFunction* fnGateForceOpen    = nullptr;
    UFunction* fnFenceDoorSolved  = nullptr;  // FenceDoor::OnPuzzleSolved
    UFunction* fnIsSolved         = nullptr;
    UFunction* fnGetPuzzleId      = nullptr;

    try { fnFenceOpen       = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Angelscript.LoweringFence:Open")); } catch (...) {}
    try { fnGateForceOpen   = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Angelscript.LockedGate:ForceOpen")); } catch (...) {}
    try { fnFenceDoorSolved = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Angelscript.FenceDoor:OnPuzzleSolved")); } catch (...) {}
    try { fnIsSolved        = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.Arranger:IsSolved")); } catch (...) {}
    try { fnGetPuzzleId     = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Talos.Arranger:GetPuzzleId")); } catch (...) {}
    if (!fnGetPuzzleId) {
        try { fnGetPuzzleId = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/Angelscript.ScriptArranger:GetPuzzleId")); } catch (...) {}
    }

    Output::send<LogLevel::Verbose>(STR("[TalosAP] F7: LoweringFence::Open={}, LockedGate::ForceOpen={}, FenceDoor::OnPuzzleSolved={}, IsSolved={}, GetPuzzleId={}\n"),
        fnFenceOpen        ? STR("found") : STR("MISSING"),
        fnGateForceOpen    ? STR("found") : STR("MISSING"),
        fnFenceDoorSolved  ? STR("found") : STR("MISSING"),
        fnIsSolved         ? STR("found") : STR("MISSING"),
        fnGetPuzzleId      ? STR("found") : STR("MISSING"));

    if (!fnFenceOpen && !fnGateForceOpen && !fnFenceDoorSolved) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] F7: Cannot proceed — no known door-open functions found\n"));
        if (hud) hud->NotifySimple(L"Debug: No door-open functions found", HudColors::TRAP);
        return;
    }

    // --- Discover the real UClass name for FenceDoor from the UFunction's owner ---
    // FindAllOf needs the exact short class name.  AngelScript-generated classes
    // may register under a name different from what the CXX header dump shows.
    std::wstring fenceDoorClassName;
    if (fnFenceDoorSolved) {
        try {
            UObject* outer = fnFenceDoorSolved->GetOuterPrivate();
            if (outer && SehProbe(outer)) {
                fenceDoorClassName = outer->GetName();
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7: FenceDoor UClass name = '{}'\n"), fenceDoorClassName);
            }
        } catch (...) {
            Output::send<LogLevel::Warning>(STR("[TalosAP] F7: Failed to get FenceDoor UClass name\n"));
        }
    }

    // --- Build arranger→door mapping from ArrangerOpensDoorScript actors ---
    // Each script has Stand (the arranger) and Door (the actor that opens).
    // Door is often null because AngelScript entity refs don't resolve via
    // C++ reflection.  When null, we read DoorInfo.EntityPointer metadata
    // to discover the door's class name and EntityID, then match by Tags.
    //
    // ArrangerOpensDoorBaseScript layout:
    //   Stand       @ 0x0330   AScriptArranger*
    //   DoorInfo    @ 0x0338   FTalosOneScriptVariableInfo (size 0x50)
    //     .EntityPointer       @ +0x18 (FTalosOneEntityPointerInfo, size 0x28)
    //       .ClassName         @ +0x00 (FString, size 0x10)
    //       .EntityID          @ +0x10 (int32)
    //       .EntityName        @ +0x18 (FString, size 0x10)
    //     .EntityPointers      @ +0x40 (TArray<FTalosOneEntityPointerInfo>)
    //   Door        @ 0x0388   AActor*

    struct ArrangerDoorLink {
        std::wstring arrangerFullName;  // Full name of Stand (the arranger)
        UObject* door;                  // Resolved door actor (may be null)
        std::wstring doorClassName;     // Class from EntityPointer (for fallback)
        int32_t doorEntityId;           // EntityID from EntityPointer (for tag matching)
    };
    std::vector<ArrangerDoorLink> doorLinks;

    {
        std::vector<UObject*> scripts;
        try { UObjectGlobals::FindAllOf(STR("ArrangerOpensDoorBaseScript"), scripts); } catch (...) {}
        try {
            std::vector<UObject*> derived;
            UObjectGlobals::FindAllOf(STR("ArrangerOpensDoorScript"), derived);
            for (auto* s : derived) {
                bool dup = false;
                for (auto* e : scripts) { if (e == s) { dup = true; break; } }
                if (!dup) scripts.push_back(s);
            }
        } catch (...) {}

        Output::send<LogLevel::Verbose>(STR("[TalosAP] F7: Found {} ArrangerOpensDoor script(s)\n"), scripts.size());

        for (auto* script : scripts) {
            if (!script) continue;
            try {
                uint8_t* base = reinterpret_cast<uint8_t*>(script);
                ArrangerDoorLink link{};
                link.doorEntityId = -1;

                // --- Read Stand (the arranger) ---
                UObject* stand = nullptr;
                ReflReadObjPtr(script, STR("Stand"), stand);
                if (!stand && SehProbe(base + 0x0330)) {
                    SehDerefObjPtr(base + 0x0330, stand);
                }
                if (!stand) continue;
                link.arrangerFullName = stand->GetFullName();

                // --- Read Door (direct pointer, often null) ---
                UObject* door = nullptr;
                ReflReadObjPtr(script, STR("Door"), door);
                if (!door && SehProbe(base + 0x0388)) {
                    SehDerefObjPtr(base + 0x0388, door);
                }
                link.door = door;

                // --- Read DoorInfo.EntityPointer for class/ID discovery ---
                // DoorInfo starts at 0x0338.
                // EntityPointer at DoorInfo+0x18 = 0x0350
                //   ClassName (FString) at +0x00 = 0x0350
                //   EntityID  (int32)   at +0x10 = 0x0360
                //   EntityName(FString) at +0x18 = 0x0368
                try {
                    // FString: first 8 bytes = Data ptr, next 4 = Num
                    wchar_t** classNameDataPtr = reinterpret_cast<wchar_t**>(base + 0x0350);
                    if (SehProbe(classNameDataPtr)) {
                        wchar_t* classNameData = *classNameDataPtr;
                        if (classNameData && SehProbe(classNameData)) {
                            link.doorClassName = classNameData;
                        }
                    }

                    int32_t* eidPtr = reinterpret_cast<int32_t*>(base + 0x0360);
                    if (SehProbe(eidPtr)) {
                        link.doorEntityId = *eidPtr;
                    }
                } catch (...) {}

                // Also try the EntityPointers TArray at DoorInfo+0x40 = 0x0378
                if (link.doorClassName.empty()) {
                    try {
                        uint8_t* epData = *reinterpret_cast<uint8_t**>(base + 0x0378);
                        int32_t  epNum  = *reinterpret_cast<int32_t*>(base + 0x0380);

                        if (epData && epNum > 0 && epNum < 100) {
                            // First entry: ClassName at +0x00, EntityID at +0x10
                            wchar_t* cn = *reinterpret_cast<wchar_t**>(epData + 0x00);
                            if (cn && SehProbe(cn)) {
                                link.doorClassName = cn;
                            }
                            link.doorEntityId = *reinterpret_cast<int32_t*>(epData + 0x10);
                        }
                    } catch (...) {}
                }

                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7:   Script={}\n"), SafeGetFullName(script));
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7:     Stand = {}\n"), SafeGetFullName(stand));
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7:     Door  = {}\n"), door ? SafeGetFullName(door) : STR("<null>"));
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7:     DoorInfo.ClassName = '{}'\n"),
                    link.doorClassName.empty() ? L"<empty>" : link.doorClassName);
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7:     DoorInfo.EntityID  = {}\n"), link.doorEntityId);

                doorLinks.push_back(std::move(link));
            } catch (...) {
                Output::send<LogLevel::Warning>(STR("[TalosAP] F7:   Exception reading script\n"));
            }
        }
    }

    Output::send<LogLevel::Verbose>(STR("[TalosAP] F7: Built {} arranger→door link(s)\n"), doorLinks.size());

    // --- Helper: check if an actor has a matching EntityID tag ---
    auto HasEntityIdTag = [](UObject* candidate, int32_t entityId) -> bool {
        if (!candidate) return false;
        try {
            auto* tagsRaw = candidate->GetValuePtrByPropertyNameInChain<uint8_t>(STR("Tags"));
            if (!tagsRaw) return false;

            uint8_t* tagData = *reinterpret_cast<uint8_t**>(tagsRaw);
            int32_t  tagNum  = *reinterpret_cast<int32_t*>(tagsRaw + sizeof(void*));
            if (!tagData || tagNum <= 0 || tagNum > 100) return false;

            constexpr size_t FNAME_SIZE = sizeof(FName);
            for (int32_t ti = 0; ti < tagNum; ti++) {
                try {
                    FName* fn = reinterpret_cast<FName*>(tagData + ti * FNAME_SIZE);
                    auto tagStr = fn->ToString();
                    auto colonPos = tagStr.find(STR(":"));
                    if (colonPos == std::wstring::npos) continue;
                    if (tagStr.substr(0, colonPos) != STR("EntityID")) continue;
                    int32_t feid = std::stoi(tagStr.substr(colonPos + 1));
                    if (feid == entityId) return true;
                } catch (...) {}
            }
        } catch (...) {}
        return false;
    };

    // --- Helper: try to find door actor by EntityID tag ---
    // Same technique as VisibilityManager::BuildFenceMap.
    auto FindActorByEntityId = [&](const wchar_t* className, int32_t entityId) -> UObject* {
        if (entityId < 0) return nullptr;

        // Map Serious Engine class names to UE/AngelScript class names
        std::vector<std::wstring> classNames;
        std::wstring cn(className);

        // CDoorEntity (Serious Engine editor class) → FenceDoor (UE AngelScript)
        if (cn == L"CDoorEntity") {
            // Use the real UClass name discovered from the UFunction, if available
            if (!fenceDoorClassName.empty()) {
                classNames.push_back(fenceDoorClassName);
            }
            classNames.push_back(L"FenceDoor");
            classNames.push_back(L"FenceDoorBase");
            classNames.push_back(L"BP_FenceDoor_C");
            classNames.push_back(L"FenceDoor_C");
            classNames.push_back(L"FenceDoorBase_C");
        }

        // Always try the original name and common variants
        classNames.push_back(cn);
        if (cn.find(L"BP_") == std::wstring::npos) {
            classNames.push_back(L"BP_" + cn + L"_C");
        }
        classNames.push_back(cn + L"_C");

        // Pass 1: search by specific class names
        for (auto& tryClass : classNames) {
            std::vector<UObject*> candidates;
            try { UObjectGlobals::FindAllOf(tryClass.c_str(), candidates); } catch (...) { continue; }
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F7:     FindAllOf('{}') → {} actor(s)\n"),
                tryClass, candidates.size());
            if (candidates.empty()) continue;

            for (auto* candidate : candidates) {
                if (HasEntityIdTag(candidate, entityId)) return candidate;
            }
        }

        // Pass 2: broad search — scan ALL actors for the EntityID tag.
        // This catches doors whose UClass name we didn't guess correctly.
        Output::send<LogLevel::Verbose>(STR("[TalosAP] F7:     Class-specific search failed, scanning ALL actors for EntityID={}\n"), entityId);
        std::vector<UObject*> allActors;
        try { UObjectGlobals::FindAllOf(STR("Actor"), allActors); } catch (...) {}
        Output::send<LogLevel::Verbose>(STR("[TalosAP] F7:     FindAllOf('Actor') → {} actor(s)\n"), allActors.size());

        for (auto* candidate : allActors) {
            if (!candidate) continue;
            if (HasEntityIdTag(candidate, entityId)) {
                // Log the class name so we can hardcode it next time
                try {
                    auto* cls = candidate->GetClassPrivate();
                    std::wstring clsName = cls ? cls->GetName() : L"<unknown>";
                    Output::send<LogLevel::Verbose>(STR("[TalosAP] F7:     Found EntityID={} on actor class='{}': {}\n"),
                        entityId, clsName, SafeGetFullName(candidate));
                } catch (...) {}
                return candidate;
            }
        }

        // Pass 3: if no EntityID tag matched, try to find FenceDoor actors
        // directly (they may not have EntityID tags at all).  Return the
        // first one found — for single-arranger levels this is correct.
        if (cn == L"CDoorEntity") {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F7:     No EntityID match, searching for FenceDoor actors directly\n"));
            for (auto& tryClass : classNames) {
                std::vector<UObject*> candidates;
                try { UObjectGlobals::FindAllOf(tryClass.c_str(), candidates); } catch (...) { continue; }
                if (!candidates.empty()) {
                    Output::send<LogLevel::Verbose>(STR("[TalosAP] F7:     Found {} actor(s) of class '{}', returning first\n"),
                        candidates.size(), tryClass);
                    return candidates[0];
                }
            }

            // Also scan all actors for anything that looks like a FenceDoor
            for (auto* candidate : allActors) {
                if (!candidate) continue;
                try {
                    auto* cls = candidate->GetClassPrivate();
                    if (!cls) continue;
                    std::wstring clsName = cls->GetName();
                    if (clsName.find(L"FenceDoor") != std::wstring::npos) {
                        Output::send<LogLevel::Verbose>(STR("[TalosAP] F7:     Found FenceDoor-like actor class='{}': {}\n"),
                            clsName, SafeGetFullName(candidate));
                        return candidate;
                    }
                } catch (...) {}
            }
        }

        return nullptr;
    };

    // --- Helper: try to open a door actor using the right function ---
    auto TryOpenDoor = [&](UObject* door) -> bool {
        if (!door) return false;

        std::wstring fullName = SafeGetFullName(door);

        // Try FenceDoor::OnPuzzleSolved (the actual arranger door type)
        if (fnFenceDoorSolved) {
            if (SafeProcessEvent(door, fnFenceDoorSolved, nullptr)) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7:   Opened via OnPuzzleSolved: {}\n"), fullName);
                return true;
            }
        }

        // Try LockedGate::ForceOpen
        if (fnGateForceOpen) {
            if (SafeProcessEvent(door, fnGateForceOpen, nullptr)) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7:   Opened via ForceOpen: {}\n"), fullName);
                return true;
            }
        }

        // Try LoweringFence::Open
        if (fnFenceOpen) {
            if (SafeProcessEvent(door, fnFenceOpen, nullptr)) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7:   Opened via Open: {}\n"), fullName);
                return true;
            }
        }

        Output::send<LogLevel::Warning>(STR("[TalosAP] F7:   Could not open door: {}\n"), fullName);
        return false;
    };

    // --- Find all BP_ArrangerDoor_C arrangers in the level ---
    std::vector<UObject*> arrangers;
    try {
        UObjectGlobals::FindAllOf(STR("BP_ArrangerDoor_C"), arrangers);
    } catch (...) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] F7: FindAllOf BP_ArrangerDoor_C failed\n"));
        return;
    }

    Output::send<LogLevel::Verbose>(STR("[TalosAP] F7: Found {} arranger door(s)\n"), arrangers.size());

    int openedCount = 0;
    int unsolvedDoorCount = 0;

    for (auto* obj : arrangers) {
        if (!obj) continue;
        try {
            // Filter by UnlockType == 1 (Door)
            uint8_t unlockType = 0;
            try {
                auto* cachedInfo = obj->GetValuePtrByPropertyNameInChain<uint8_t>(STR("CachedPuzzleInfo"));
                if (cachedInfo) {
                    unlockType = *(cachedInfo + 0x08);
                } else { continue; }
            } catch (...) { continue; }

            if (unlockType != 1) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7: Skipping arranger with UnlockType={}\n"), unlockType);
                continue;
            }

            // Get PuzzleId for logging
            std::wstring puzzleIdStr = L"<unknown>";
            if (fnGetPuzzleId) {
                try {
                    struct { FString ReturnValue; } pidParams{};
                    obj->ProcessEvent(fnGetPuzzleId, &pidParams);
                    const wchar_t* raw = *pidParams.ReturnValue;
                    if (raw && raw[0] != L'\0') puzzleIdStr = raw;
                } catch (...) {}
            }

            // Check if already solved
            if (fnIsSolved) {
                try {
                    struct { bool ReturnValue; } isSolvedParams{};
                    obj->ProcessEvent(fnIsSolved, &isSolvedParams);
                    if (isSolvedParams.ReturnValue) {
                        Output::send<LogLevel::Verbose>(STR("[TalosAP] F7: '{}' already solved, skipping\n"), puzzleIdStr);
                        continue;
                    }
                } catch (...) {}
            }

            unsolvedDoorCount++;
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F7: Processing unsolved door '{}'\n"), puzzleIdStr);

            // --- Find the associated door from our mapping ---
            std::wstring arrangerName = obj->GetFullName();
            UObject* door = nullptr;
            std::wstring doorClassName;
            int32_t doorEntityId = -1;

            for (auto& link : doorLinks) {
                if (link.arrangerFullName == arrangerName) {
                    door = link.door;
                    doorClassName = link.doorClassName;
                    doorEntityId = link.doorEntityId;
                    break;
                }
            }

            // If direct Door pointer is null, resolve by EntityID tag
            if (!door && !doorClassName.empty() && doorEntityId >= 0) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7:   Resolving door by class='{}' EntityID={}\n"),
                    doorClassName, doorEntityId);
                door = FindActorByEntityId(doorClassName.c_str(), doorEntityId);
                if (door) {
                    Output::send<LogLevel::Verbose>(STR("[TalosAP] F7:   Resolved door: {}\n"), SafeGetFullName(door));
                }
            }

            if (door) {
                if (TryOpenDoor(door)) {
                    openedCount++;
                } else {
                    Output::send<LogLevel::Warning>(STR("[TalosAP] F7:   Failed to open door for '{}'\n"), puzzleIdStr);
                }
            } else {
                Output::send<LogLevel::Warning>(STR("[TalosAP] F7:   No door found for '{}' (class='{}' eid={})\n"),
                    puzzleIdStr, doorClassName.empty() ? L"<unknown>" : doorClassName, doorEntityId);
            }
        } catch (...) {
            Output::send<LogLevel::Warning>(STR("[TalosAP] F7: Exception processing arranger\n"));
        }
    }

    // --- Fallback: if no doors were opened, do diagnostic scan ---
    if (unsolvedDoorCount > 0 && openedCount == 0) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] F7: Targeted approach failed — running diagnostic scan\n"));

        // DIAGNOSTIC: scan ALL actors for class names related to doors/gates
        // This will tell us what the physical gate actor actually is.
        std::vector<UObject*> allActors;
        try { UObjectGlobals::FindAllOf(STR("Actor"), allActors); } catch (...) {}

        Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG: Scanning {} actors for door/gate-related classes\n"), allActors.size());

        // Collect unique class names matching gate-related keywords
        std::vector<std::wstring> gateKeywords = {
            L"Door", L"Gate", L"Fence", L"Wall", L"Barrier",
            L"Block", L"Lock", L"Puzzle", L"Activat"
        };
        std::vector<std::pair<std::wstring, int>> uniqueClasses;  // className -> count

        for (auto* actor : allActors) {
            if (!actor) continue;
            try {
                auto* cls = actor->GetClassPrivate();
                if (!cls) continue;
                std::wstring clsName = cls->GetName();

                bool match = false;
                for (auto& kw : gateKeywords) {
                    // Case-insensitive find
                    std::wstring lower = clsName;
                    std::wstring lowerKw = kw;
                    for (auto& c : lower) c = towlower(c);
                    for (auto& c : lowerKw) c = towlower(c);
                    if (lower.find(lowerKw) != std::wstring::npos) {
                        match = true;
                        break;
                    }
                }

                if (match) {
                    bool found = false;
                    for (auto& p : uniqueClasses) {
                        if (p.first == clsName) { p.second++; found = true; break; }
                    }
                    if (!found) uniqueClasses.push_back({clsName, 1});
                }
            } catch (...) {}
        }

        for (auto& p : uniqueClasses) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:   Class='{}' x{}\n"), p.first, p.second);
        }

        // DIAGNOSTIC: also check actors that have PuzzleActorComp/PuzzleActorComponent
        Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG: Scanning for actors with PuzzleActorComp property\n"));
        int puzzleActorCount = 0;
        for (auto* actor : allActors) {
            if (!actor) continue;
            try {
                UObject* puzzleComp = nullptr;
                if (ReflReadObjPtr(actor, STR("PuzzleActorComp"), puzzleComp) && puzzleComp) {
                    auto* cls = actor->GetClassPrivate();
                    std::wstring clsName = cls ? cls->GetName() : L"<unknown>";
                    Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:   PuzzleActorComp on class='{}': {}\n"),
                        clsName, SafeGetFullName(actor));
                    puzzleActorCount++;
                }
            } catch (...) {}
        }
        if (puzzleActorCount == 0) {
            // Try alternate property name
            for (auto* actor : allActors) {
                if (!actor) continue;
                try {
                    UObject* puzzleComp = nullptr;
                    if (ReflReadObjPtr(actor, STR("PuzzleActorComponent"), puzzleComp) && puzzleComp) {
                        auto* cls = actor->GetClassPrivate();
                        std::wstring clsName = cls ? cls->GetName() : L"<unknown>";
                        Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:   PuzzleActorComponent on class='{}': {}\n"),
                            clsName, SafeGetFullName(actor));
                        puzzleActorCount++;
                    }
                } catch (...) {}
            }
        }
        Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG: Found {} actor(s) with PuzzleActorComp/Component\n"), puzzleActorCount);

        // DIAGNOSTIC: log actor full names in the same sublevel as the arranger
        // The arranger is in Cloud_1_01 — look for non-common actors nearby
        Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG: Class names containing 'Arranger':\n"));
        for (auto* actor : allActors) {
            if (!actor) continue;
            try {
                auto* cls = actor->GetClassPrivate();
                if (!cls) continue;
                std::wstring clsName = cls->GetName();
                if (clsName.find(L"Arranger") != std::wstring::npos) {
                    Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:   {}: {}\n"), clsName, SafeGetFullName(actor));
                }
            } catch (...) {}
        }
    }

    Output::send<LogLevel::Verbose>(STR("[TalosAP] F7: Opened {} door(s) for {} unsolved door arranger(s)\n"),
        openedCount, unsolvedDoorCount);

    if (hud) {
        if (openedCount > 0) {
            std::wstring msg = L"Debug: Opened " + std::to_wstring(openedCount) + L" door(s)";
            hud->NotifySimple(msg, HudColors::SERVER);
        } else if (unsolvedDoorCount == 0) {
            hud->NotifySimple(L"Debug: No unsolved door arrangers in this level", HudColors::SERVER);
        } else {
            hud->NotifySimple(L"Debug: Found arrangers but could not open doors (check log)", HudColors::TRAP);
        }
    }
}

// ============================================================
// Diagnostic: Dump arranger actor state via reflection
// Called before and after SolveArranger to detect changes.
// ============================================================

void DebugCommands::DumpArrangerState(UObject* arranger, const std::wstring& puzzleId, const wchar_t* label)
{
    Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG: === {} State for '{}' ===\n"), label, puzzleId);
    Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:   Actor = {}\n"), SafeGetFullName(arranger));

    // ---- Boolean flags (all via reflection) ----
    const wchar_t* boolProps[] = {
        STR("bCreateCheckpointOnSolved"),
        STR("bIsEditingPuzzle"),
        STR("bHasWidget"),
        STR("bFakeArranger"),
        STR("bCustomArranger"),
    };
    for (const auto* propName : boolProps) {
        bool val = false;
        if (ReflReadBool(arranger, propName, val)) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:   {} = {}\n"),
                propName, val ? STR("true") : STR("false"));
        } else {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:   {} = <not found or AV>\n"), propName);
        }
    }

    // ---- UObject* component / pointer properties ----
    // Log whether each is null, populated, or stale.
    const wchar_t* objProps[] = {
        STR("LerpInArrangerHud"),       // UTimelineComponent*
        STR("LerpBackArrangerHud"),     // UTimelineComponent*
        STR("Tween Manager"),           // UTweenManagerComponent*
        STR("ArrangerHUD"),             // UBP_ArrangerWidget_C*
        STR("SpotLight"),               // USpotLightComponent*
        STR("Camera"),                  // UCameraComponent*
        STR("ArrangerMesh"),            // UStaticMeshComponent*
        STR("StandMesh"),               // USkeletalMeshComponent*
        STR("AudioComponent"),          // UAkComponent*
    };
    for (const auto* propName : objProps) {
        UObject* val = nullptr;
        if (ReflReadObjPtr(arranger, propName, val)) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:   {} = {}\n"),
                propName, val ? SafeGetFullName(val) : STR("<null>"));
        } else {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:   {} = <not found or AV>\n"), propName);
        }
    }

    // ---- InteractingCharacter (TWeakObjectPtr — not a plain UObject*) ----
    // Read raw 8 bytes:  {int32 ObjectIndex, int32 SerialNumber}
    // If both are 0 or -1, the weak ptr is empty/stale.  A non-zero value
    // means something is still referencing a character — suspicious if we
    // never opened an arranger UI.
    {
        uint64_t raw = 0;
        if (ReflReadRaw8(arranger, STR("InteractingCharacter"), raw)) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:   InteractingCharacter (WeakPtr raw) = 0x{:016X} {}\n"),
                raw, raw == 0 ? STR("(empty)") : STR("(ACTIVE — unexpected!)"));
        } else {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:   InteractingCharacter = <not found or AV>\n"));
        }
    }

    // ---- Timeline direction enums (property names include BP GUIDs) ----
    // TEnumAsByte<ETimelineDirection::Type>: Forward=0, Reverse=1
    {
        uint8_t dirIn = 0xFF;
        if (ReflReadU8(arranger, STR("LerpInArrangerHud__Direction_D94B15AD4A115A98710B6ABB4F150EFF"), dirIn)) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:   LerpInDirection = {} (0=Fwd, 1=Rev)\n"), dirIn);
        }
        uint8_t dirBack = 0xFF;
        if (ReflReadU8(arranger, STR("LerpBackArrangerHud__Direction_EC78CDA0417759E198AA07922772655D"), dirBack)) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:   LerpBackDirection = {} (0=Fwd, 1=Rev)\n"), dirBack);
        }
    }

    // ---- SeenAnimationCooldown (double) ----
    {
        double cooldown = 0.0;
        if (ReflReadDouble(arranger, STR("SeenAnimationCooldown"), cooldown)) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:   SeenAnimationCooldown = {:.4f}\n"), cooldown);
        }
    }

    Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG: === End {} State ===\n"), label);
}

// ============================================================
// Diagnostic: Scan actors that link arrangers to doors/fences.
// These are prime crash suspects — if their animations, movers,
// or audio are still active during level teardown, they can hold
// stale references.
// ============================================================

void DebugCommands::DumpRelatedActors()
{
    Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG: === Related Actor Scan ===\n"));

    // ---- ArrangerOpensDoorScript / ArrangerOpensDoorBaseScript ----
    // These AngelScript actors link Stand (arranger) → Door (the actor
    // that physically opens).  If the Door has an active mover/animation
    // post-solve, it might crash on level unload.
    {
        std::vector<UObject*> scripts;
        try { UObjectGlobals::FindAllOf(STR("ArrangerOpensDoorBaseScript"), scripts); } catch (...) {}
        try {
            std::vector<UObject*> derived;
            UObjectGlobals::FindAllOf(STR("ArrangerOpensDoorScript"), derived);
            for (auto* s : derived) {
                bool dup = false;
                for (auto* e : scripts) { if (e == s) { dup = true; break; } }
                if (!dup) scripts.push_back(s);
            }
        } catch (...) {}

        Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG: ArrangerOpensDoor scripts: {}\n"), scripts.size());

        for (auto* script : scripts) {
            if (!script) continue;

            Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:   Script = {}\n"), SafeGetFullName(script));

            // Stand = the arranger this script watches
            UObject* stand = nullptr;
            if (ReflReadObjPtr(script, STR("Stand"), stand)) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:     Stand = {}\n"),
                    stand ? SafeGetFullName(stand) : STR("<null>"));
            } else {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:     Stand = <prop not found>\n"));
            }

            // Door = the actor that opens (could be a MovingPlatform, etc.)
            UObject* door = nullptr;
            if (ReflReadObjPtr(script, STR("Door"), door)) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:     Door = {}\n"),
                    door ? SafeGetFullName(door) : STR("<null>"));

                if (door) {
                    // Log the door's RootComponent class — useful for identifying
                    // if it's a skeletal mesh (animation), static mesh, etc.
                    UObject* rootComp = nullptr;
                    if (ReflReadObjPtr(door, STR("RootComponent"), rootComp) && rootComp) {
                        Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:     Door.Root = {}\n"),
                            SafeGetFullName(rootComp));
                    }
                    // PrimaryActorTick.bStartWithTickEnabled — is the door ticking?
                    bool bTicking = false;
                    if (ReflReadBool(door, STR("bActorIsBeingDestroyed"), bTicking)) {
                        Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:     Door.bActorIsBeingDestroyed = {}\n"),
                            bTicking ? STR("true") : STR("false"));
                    }
                }
            } else {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:     Door = <prop not found>\n"));
            }
        }
    }

    // ---- LoweringFence actors ----
    // These have skeletal mesh animations and Wwise audio that may still
    // be playing during level teardown.
    {
        std::vector<UObject*> fences;
        try { UObjectGlobals::FindAllOf(STR("LoweringFence"), fences); } catch (...) {}

        Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG: LoweringFence actors: {}\n"), fences.size());

        for (auto* fence : fences) {
            if (!fence) continue;

            Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:   Fence = {}\n"), SafeGetFullName(fence));

            UObject* mesh = nullptr;
            if (ReflReadObjPtr(fence, STR("Mesh"), mesh)) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:     Mesh = {}\n"),
                    mesh ? SafeGetFullName(mesh) : STR("<null>"));
            }

            UObject* akComp = nullptr;
            if (ReflReadObjPtr(fence, STR("AkComponent"), akComp)) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:     AkComponent = {}\n"),
                    akComp ? SafeGetFullName(akComp) : STR("<null>"));
            }

            UObject* audioEvt = nullptr;
            if (ReflReadObjPtr(fence, STR("AudioEvent"), audioEvt)) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:     AudioEvent = {}\n"),
                    audioEvt ? SafeGetFullName(audioEvt) : STR("<null>"));
            }
        }
    }

    // ---- LoweringFenceWhenTetrominoIsPickedUpBaseScript ----
    // These scripts wire a tetromino pickup to a fence open.
    // Log their Tetromino and LoweringFence references via reflection.
    {
        std::vector<UObject*> scripts;
        try { UObjectGlobals::FindAllOf(STR("LoweringFenceWhenTetrominoIsPickedUpBaseScript"), scripts); } catch (...) {}
        try {
            std::vector<UObject*> derived;
            UObjectGlobals::FindAllOf(STR("LoweringFenceWhenTetrominoIsPickedUpScript"), derived);
            for (auto* s : derived) {
                bool dup = false;
                for (auto* e : scripts) { if (e == s) { dup = true; break; } }
                if (!dup) scripts.push_back(s);
            }
        } catch (...) {}

        Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG: LoweringFenceWhenTetromino scripts: {}\n"), scripts.size());

        for (auto* script : scripts) {
            if (!script) continue;

            Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:   Script = {}\n"), SafeGetFullName(script));

            UObject* tet = nullptr;
            if (ReflReadObjPtr(script, STR("Tetromino"), tet)) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:     Tetromino = {}\n"),
                    tet ? SafeGetFullName(tet) : STR("<null>"));
            } else {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:     Tetromino = <prop not found>\n"));
            }

            UObject* fence = nullptr;
            if (ReflReadObjPtr(script, STR("LoweringFence"), fence)) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:     LoweringFence = {}\n"),
                    fence ? SafeGetFullName(fence) : STR("<null>"));
            } else {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG:     LoweringFence = <prop not found>\n"));
            }
        }
    }

    Output::send<LogLevel::Verbose>(STR("[TalosAP] F7-DIAG: === End Related Actor Scan ===\n"));
}

} // namespace TalosAP
