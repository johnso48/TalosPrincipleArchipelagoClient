#define NOMINMAX
#include <windows.h>

#include "headers/MechanicsHandler.h"
#include "headers/InventorySync.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/Hooks.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace TalosAP {

// ============================================================
// SEH-safe wrappers
// ============================================================

static bool MH_SEH_ProcessEvent(UObject* obj, UFunction* fn, void* params)
{
    __try { obj->ProcessEvent(fn, params); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static UObject* MH_SEH_FindFirstOf(const wchar_t* className)
{
    __try { return UObjectGlobals::FindFirstOf(className); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static bool MH_SEH_WriteByte(void* addr, uint8_t value)
{
    __try {
        *reinterpret_cast<uint8_t*>(addr) = value;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static uint8_t MH_SEH_ReadByte(void* addr, uint8_t fallback)
{
    __try {
        return *reinterpret_cast<uint8_t*>(addr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return fallback;
    }
}

// ============================================================
// Mechanic arranger lookup table
// ============================================================

// EPuzzleMechanic bitmask: Time=1, Cube=2, Fan=4, Rod=8, Platform=16
struct MechanicArrangerInfo {
    uint8_t        bitmask;       // EPuzzleMechanic value
    const wchar_t* arrangerId;    // Arranger puzzle ID string
    const wchar_t* label;         // Human-readable name
};

static constexpr MechanicArrangerInfo MECHANIC_ARRANGERS[] = {
    { 0x01, L"MechanicTime",   L"Time"     },
    { 0x02, L"MechanicCube",   L"Cube"     },
    { 0x04, L"MechanicFan",    L"Fan"      },
    { 0x08, L"MechanicRods",   L"Rod"      },
    { 0x10, L"MechanicShield", L"Platform" },
};

// ============================================================
// RegisterHooks — IsMechanicUnlocked post-hooks
// ============================================================

void MechanicsHandler::RegisterHooks(ModState& state)
{
    // Hook: PuzzleMemoryFunctions::IsMechanicUnlocked
    // Post-hook overrides the return value to TRUE when the queried mechanic
    // is in our UnlockedMechanicsMask.  This fixes the puzzle-entry HUD
    // showing a red "locked mechanic" flash even though gameplay gating is
    // already removed via RequiredMechanics bit-clearing.
    try {
        UObjectGlobals::RegisterHook(
            STR("/Script/Talos.PuzzleMemoryFunctions:IsMechanicUnlocked"),
            {},  // no pre-callback
            [](UnrealScriptFunctionCallableContext& ctx, void* data) {
                auto* st = static_cast<ModState*>(data);

                // Param struct mirrors:
                //   bool IsMechanicUnlocked(const UObject* Context,
                //                           EPuzzleMechanic Mechanic,
                //                           UTalosProgress* UsedProgress)
                struct Params {
                    UObject*  Context;
                    uint8_t   Mechanic;      // EPuzzleMechanic bitmask value
                    uint8_t   _pad[7];
                    UObject*  UsedProgress;
                    bool      ReturnValue;
                };
                auto& p = ctx.GetParams<Params>();
                uint8_t mask = st->UnlockedMechanicsMask.load();

                if ((mask & p.Mechanic) != 0 && !p.ReturnValue) {
                    p.ReturnValue = true;
                    ctx.SetReturnValue<bool>(true);
                    Output::send<LogLevel::Verbose>(
                        STR("[TalosAP] Hook IsMechanicUnlocked: mechanic 0x{:02X} overridden to TRUE\n"),
                        p.Mechanic);
                }
            },
            &state
        );
        Output::send<LogLevel::Verbose>(STR("[TalosAP] Hooked: PuzzleMemoryFunctions::IsMechanicUnlocked (post)\n"));
    }
    catch (...) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] Failed to hook PuzzleMemoryFunctions::IsMechanicUnlocked\n"));
    }

    // Hook: CarriableComponent::IsMechanicUnlocked (no params version)
    // This is the per-component check used by the carry/interact system.
    // If RequiredMechanics bits have been cleared, it should already return
    // true, but hooking it as belt-and-suspenders ensures the UI path works.
    try {
        UObjectGlobals::RegisterHook(
            STR("/Script/Talos.CarriableComponent:IsMechanicUnlocked"),
            {},  // no pre-callback
            [](UnrealScriptFunctionCallableContext& ctx, void* data) {
                auto* st = static_cast<ModState*>(data);
                uint8_t mask = st->UnlockedMechanicsMask.load();
                if (mask == 0) return;  // nothing unlocked by us, don't interfere

                // Read RequiredMechanics from the component (offset 0x02A0)
                uintptr_t base = reinterpret_cast<uintptr_t>(ctx.Context);
                uint8_t required = 0;
                __try { required = *reinterpret_cast<uint8_t*>(base + 0x02A0); }
                __except(EXCEPTION_EXECUTE_HANDLER) { return; }

                // If all required mechanics are covered by our mask, force true
                if ((required & ~mask) == 0) {
                    ctx.SetReturnValue<bool>(true);
                }
            },
            &state
        );
        Output::send<LogLevel::Verbose>(STR("[TalosAP] Hooked: CarriableComponent::IsMechanicUnlocked (post)\n"));
    }
    catch (...) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] Failed to hook CarriableComponent::IsMechanicUnlocked\n"));
    }
}

// ============================================================
// ProcessPending — save-data + runtime mechanic unlock
// ============================================================

void MechanicsHandler::ProcessPending(ModState& state)
{
    if (!state.PendingMechanicsPatch.exchange(false)) return;

    Output::send<LogLevel::Verbose>(STR("[TalosAP] MechanicsPatch: Starting selective mechanic unlock\n"));

    // Get the mask of mechanics that should be unlocked
    uint8_t targetMask = state.UnlockedMechanicsMask.load();
    Output::send<LogLevel::Verbose>(STR("[TalosAP] MechanicsPatch: Target mask = 0x{:02X}\n"), targetMask);

    // Ensure we have a progress object
    InventorySync::FindProgressObject(state);
    UObject* progress = state.CurrentProgress;
    if (!progress) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] MechanicsPatch: No progress object — will retry\n"));
        state.PendingMechanicsPatch.store(true);
        return;
    }

    // Get world context
    UObject* worldCtx = MH_SEH_FindFirstOf(STR("PlayerController"));
    if (!worldCtx) worldCtx = MH_SEH_FindFirstOf(STR("TalosGameInstance"));
    if (!worldCtx) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] MechanicsPatch: No world context — will retry\n"));
        state.PendingMechanicsPatch.store(true);
        return;
    }

    // Resolve UFunctions (cached via static)
    static UFunction* fnSetArrangerSolved = nullptr;
    static UFunction* fnStoreTetrominoLoc = nullptr;
    static UFunction* fnIsArrangerSolved  = nullptr;
    static UFunction* fnIsMechUnlocked    = nullptr;
    static UFunction* fnBoolSet           = nullptr;
    static UObject*   pmfCDO              = nullptr;
    static bool       fnResolved          = false;

    if (!fnResolved) {
        fnResolved = true;
        try { fnSetArrangerSolved = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
            STR("/Script/Talos.PuzzleMemoryFunctions:SetArrangerSolved")); } catch (...) {}
        try { fnStoreTetrominoLoc = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
            STR("/Script/Talos.TalosProgress:StoreTetrominoLocationsForArranger")); } catch (...) {}
        try { fnIsArrangerSolved = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
            STR("/Script/Talos.PuzzleMemoryFunctions:IsArrangerSolved")); } catch (...) {}
        try { fnIsMechUnlocked = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
            STR("/Script/Talos.PuzzleMemoryFunctions:IsMechanicUnlocked")); } catch (...) {}
        try { fnBoolSet = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr,
            STR("/Script/Talos.TalosProgress:BoolSet")); } catch (...) {}
        try { pmfCDO = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr,
            STR("/Script/Talos.Default__PuzzleMemoryFunctions")); } catch (...) {}

        Output::send<LogLevel::Verbose>(STR("[TalosAP] MechanicsPatch: Resolved UFunctions: SetArrangerSolved={} StoreTetLoc={} IsArrangerSolved={} IsMechUnlocked={} BoolSet={} pmfCDO={}\n"),
            fnSetArrangerSolved ? STR("ok") : STR("MISS"),
            fnStoreTetrominoLoc ? STR("ok") : STR("MISS"),
            fnIsArrangerSolved  ? STR("ok") : STR("MISS"),
            fnIsMechUnlocked    ? STR("ok") : STR("MISS"),
            fnBoolSet           ? STR("ok") : STR("MISS"),
            pmfCDO              ? STR("ok") : STR("MISS"));
    }

    if (!pmfCDO) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] MechanicsPatch: pmfCDO not found — cannot unlock mechanics\n"));
        return;
    }

    // --- Process each mechanic ---
    for (const auto& info : MECHANIC_ARRANGERS) {
        bool shouldUnlock = (targetMask & info.bitmask) != 0;

        if (!shouldUnlock) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] MechanicsPatch: {} — not in target mask, skipping\n"), info.label);
            continue;
        }

        // Also set the legacy/dialog-style GameMemory flag as an additive side effect.
        // This is not the primary unlock mechanism, but keeps save globals consistent
        // with a naturally unlocked mechanic.
        if (fnBoolSet) {
            std::wstring unlockVar = L"Unlocked_";
            unlockVar += info.arrangerId;

            struct {
                FString Name;
                bool    bValue;
            } boolP{};
            boolP.Name = FString(unlockVar.c_str());
            boolP.bValue = true;

            if (MH_SEH_ProcessEvent(progress, fnBoolSet, &boolP)) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] MechanicsPatch:   BoolSet('{}', true) OK\n"), unlockVar);
            } else {
                Output::send<LogLevel::Warning>(STR("[TalosAP] MechanicsPatch:   BoolSet('{}', true) FAILED\n"), unlockVar);
            }

            // Belt-and-suspenders: the fifth mechanic is represented as MechanicShield
            // in arranger data but may be referenced as MechanicPlatform elsewhere.
            if (info.bitmask == 0x10) {
                struct {
                    FString Name;
                    bool    bValue;
                } platformBoolP{};
                platformBoolP.Name = FString(L"Unlocked_MechanicPlatform");
                platformBoolP.bValue = true;

                if (MH_SEH_ProcessEvent(progress, fnBoolSet, &platformBoolP)) {
                    Output::send<LogLevel::Verbose>(STR("[TalosAP] MechanicsPatch:   BoolSet('Unlocked_MechanicPlatform', true) OK\n"));
                }
            }
        }

        // Check if already solved
        bool alreadySolved = false;
        if (fnIsArrangerSolved) {
            struct {
                UObject* Context;
                FString  ArrangerPuzzleId;
                UObject* UsedProgress;
                bool     ReturnValue;
            } p{};
            p.Context = worldCtx;
            p.ArrangerPuzzleId = FString(info.arrangerId);
            p.UsedProgress = progress;
            p.ReturnValue = false;
            if (MH_SEH_ProcessEvent(pmfCDO, fnIsArrangerSolved, &p)) {
                alreadySolved = p.ReturnValue;
            }
        }

        if (alreadySolved) {
            Output::send<LogLevel::Verbose>(STR("[TalosAP] MechanicsPatch: {} — already solved\n"), info.label);
            continue;
        }

        Output::send<LogLevel::Verbose>(STR("[TalosAP] MechanicsPatch: Unlocking {} (arranger '{}')\n"),
            info.label, info.arrangerId);

        // Step 1: Store fake tetromino locations so the TMap entry exists.
        if (fnStoreTetrominoLoc) {
            TArray<FString> fakeLocations;
            fakeLocations.Add(FString(L"P=0.000000 Y=0.000000 R=90.000000;X=-0.792 Y=1.000 Z=27.902;X=0.050 Y=0.050 Z=0.050"));
            fakeLocations.Add(FString(L"P=-90.000000 Y=0.000000 R=90.000000;X=6.708 Y=1.000 Z=30.402;X=0.050 Y=0.050 Z=0.050"));
            fakeLocations.Add(FString(L"P=0.000000 Y=0.000000 R=90.000000;X=-0.792 Y=1.000 Z=32.902;X=0.050 Y=0.050 Z=0.050"));

            struct {
                FString           PuzzleName;
                TArray<FString>   InLocations;
            } storeP{};
            storeP.PuzzleName = FString(info.arrangerId);
            storeP.InLocations = fakeLocations;

            if (MH_SEH_ProcessEvent(progress, fnStoreTetrominoLoc, &storeP)) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] MechanicsPatch:   StoreTetrominoLocationsForArranger('{}', 3 pieces) OK\n"),
                    info.arrangerId);
            } else {
                Output::send<LogLevel::Warning>(STR("[TalosAP] MechanicsPatch:   StoreTetrominoLocationsForArranger FAILED\n"));
            }
        }

        // Step 2: Call SetArrangerSolved to finalize the solved state
        if (fnSetArrangerSolved) {
            struct {
                UObject* Context;
                FString  ArrangerPuzzleId;
                bool     bSolved;
            } solveP{};
            solveP.Context = worldCtx;
            solveP.ArrangerPuzzleId = FString(info.arrangerId);
            solveP.bSolved = true;

            if (MH_SEH_ProcessEvent(pmfCDO, fnSetArrangerSolved, &solveP)) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] MechanicsPatch:   SetArrangerSolved('{}', true) OK\n"),
                    info.arrangerId);
            } else {
                Output::send<LogLevel::Warning>(STR("[TalosAP] MechanicsPatch:   SetArrangerSolved FAILED\n"));
            }
        }

        // Step 3: Verify with IsMechanicUnlocked
        if (fnIsMechUnlocked) {
            struct {
                UObject*  Context;
                uint8_t   Mechanic;
                uint8_t   _pad[7];
                UObject*  UsedProgress;
                bool      ReturnValue;
            } checkP{};
            checkP.Context = worldCtx;
            checkP.Mechanic = info.bitmask;
            checkP.UsedProgress = progress;
            checkP.ReturnValue = false;

            if (MH_SEH_ProcessEvent(pmfCDO, fnIsMechUnlocked, &checkP)) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] MechanicsPatch:   IsMechanicUnlocked({}) = {} {}\n"),
                    info.label, checkP.ReturnValue ? STR("TRUE") : STR("FALSE"),
                    checkP.ReturnValue ? STR("SUCCESS!") : STR("— may need level reload"));
            }
        }

        // Step 4: Also verify with IsArrangerSolved
        if (fnIsArrangerSolved) {
            struct {
                UObject* Context;
                FString  ArrangerPuzzleId;
                UObject* UsedProgress;
                bool     ReturnValue;
            } verifyP{};
            verifyP.Context = worldCtx;
            verifyP.ArrangerPuzzleId = FString(info.arrangerId);
            verifyP.UsedProgress = progress;
            verifyP.ReturnValue = false;

            if (MH_SEH_ProcessEvent(pmfCDO, fnIsArrangerSolved, &verifyP)) {
                Output::send<LogLevel::Verbose>(STR("[TalosAP] MechanicsPatch:   IsArrangerSolved('{}') = {}\n"),
                    info.arrangerId, verifyP.ReturnValue ? STR("TRUE") : STR("FALSE"));
            }
        }
    }

    // --- Runtime enforcement: selectively clear RequiredMechanics bits ---
    if (targetMask != 0) {
        std::vector<UObject*> carriables;
        try { UObjectGlobals::FindAllOf(STR("CarriableComponent"), carriables); } catch (...) {}

        int patched = 0;
        for (UObject* comp : carriables) {
            if (!comp) continue;
            uintptr_t base = reinterpret_cast<uintptr_t>(comp);
            uint8_t oldVal = MH_SEH_ReadByte(reinterpret_cast<void*>(base + 0x02A0), 0);
            uint8_t newVal = oldVal & ~targetMask;
            if (newVal != oldVal) {
                if (MH_SEH_WriteByte(reinterpret_cast<void*>(base + 0x02A0), newVal)) {
                    ++patched;
                    Output::send<LogLevel::Verbose>(STR("[TalosAP] MechanicsPatch:   Carriable {:016X} RequiredMechanics 0x{:02X} -> 0x{:02X}\n"),
                        base, oldVal, newVal);
                }
            }
        }
        Output::send<LogLevel::Verbose>(STR("[TalosAP] MechanicsPatch: Patched {}/{} CarriableComponents (mask 0x{:02X})\n"),
            patched, carriables.size(), targetMask);
    }

    Output::send<LogLevel::Verbose>(STR("[TalosAP] MechanicsPatch: Done\n"));
}

} // namespace TalosAP
