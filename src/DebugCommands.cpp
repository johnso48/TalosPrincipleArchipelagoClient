#include "headers/DebugCommands.h"
#include "headers/InventorySync.h"

#include <DynamicOutput/DynamicOutput.hpp>

using namespace RC;

namespace TalosAP {

void DebugCommands::ProcessPending(ModState& state,
                                   ItemMapping& itemMapping,
                                   VisibilityManager& visibilityManager,
                                   HudNotification* hud)
{
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

} // namespace TalosAP
