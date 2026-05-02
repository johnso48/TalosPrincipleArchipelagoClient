#define NOMINMAX
#include <windows.h>
#include <unordered_set>
#include <string>

#include "headers/DoorUnlockHandler.h"
#include "headers/InventorySync.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/FString.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace TalosAP {

// ============================================================
// SEH-safe wrappers
// ============================================================

static UObject* DUH_SEH_FindFirstOf(const wchar_t* className)
{
    __try { return UObjectGlobals::FindFirstOf(className); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static bool DUH_SEH_ProcessEvent(UObject* obj, UFunction* fn, void* params)
{
    __try { obj->ProcessEvent(fn, params); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static UFunction* DUH_SEH_StaticFindFunction(const wchar_t* path)
{
    __try {
        return UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, path);
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// ============================================================
// ProcessPending — write BoolSet("Unlocked_<doorId>", true)
//
// Gate opening at runtime is no longer done here. Instead, the
// APClient grants Door-type tetromino pieces when a gate item
// is received, and the player solves the arranger manually.
// The BoolSet ensures the gate opens automatically on level load
// if it was already solved or if the save is reloaded.
// ============================================================

void DoorUnlockHandler::ProcessPending(ModState& state)
{
    if (!state.PendingDoorUnlockEnforce.exchange(false)) return;
    if (state.UnlockedDoors.empty()) return;

    Output::send<LogLevel::Verbose>(STR("[TalosAP] DoorUnlock: Enforcing {} door(s) in save data\n"),
        state.UnlockedDoors.size());

    // Ensure we have a fresh progress object
    InventorySync::FindProgressObject(state);
    UObject* progress = state.CurrentProgress;
    if (!progress) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] DoorUnlock: No progress object — will retry next trigger\n"));
        state.PendingDoorUnlockEnforce.store(true);
        return;
    }

    // Resolve BoolSet UFunction (cached via static)
    static UFunction* fnBoolSet  = nullptr;
    static bool       fnResolved = false;

    if (!fnResolved) {
        fnResolved = true;
        fnBoolSet = DUH_SEH_StaticFindFunction(STR("/Script/Talos.TalosProgress:BoolSet"));

        Output::send<LogLevel::Verbose>(STR("[TalosAP] DoorUnlock: Resolved BoolSet={}\n"),
            fnBoolSet ? STR("ok") : STR("MISS"));
    }

    // Track which doors we've already written save data for (avoid repeating on retries)
    static std::unordered_set<std::string> saveDataWritten;

    for (const auto& doorId : state.UnlockedDoors) {
        if (saveDataWritten.count(doorId)) continue;

        std::wstring wideDoorId(doorId.begin(), doorId.end());

        Output::send<LogLevel::Verbose>(STR("[TalosAP] DoorUnlock: Setting save data for '{}'\n"), wideDoorId);

        // BoolSet("Unlocked_<doorId>", true) on progress
        if (fnBoolSet) {
            std::wstring unlockVar = L"Unlocked_" + wideDoorId;
            struct {
                FString Name;
                bool bValue;
            } p{};
            p.Name = FString(unlockVar.c_str());
            p.bValue = true;
            if (DUH_SEH_ProcessEvent(progress, fnBoolSet, &p)) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] DoorUnlock:   BoolSet('{}', true) OK\n"), unlockVar);
                saveDataWritten.insert(doorId);
            }
        }
    }

    Output::send<LogLevel::Verbose>(STR("[TalosAP] DoorUnlock: Enforcement complete\n"));
}

} // namespace TalosAP
