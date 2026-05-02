#include <atomic>
#include <Mod/CppUserModBase.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <Input/Handler.hpp>

#include "src/headers/ModCore.h"

using namespace RC;

/// Thin DLL shell — all game logic lives in ModCore.
class TalosPrincipleArchipelagoMod : public RC::CppUserModBase
{
public:
    TalosPrincipleArchipelagoMod() : CppUserModBase()
    {
        ModName = STR("TalosPrincipleArchipelago");
        ModVersion = STR("0.1.0");
        ModDescription = STR("Archipelago multiworld integration for The Talos Principle Reawakened");
        ModAuthors = STR("Froddo");

        Output::send<LogLevel::Verbose>(STR("[TalosAP] Mod constructed\n"));
    }

    ~TalosPrincipleArchipelagoMod() override
    {
        // Signal on_update to stop all UObject work immediately.
        // During engine teardown UObjects are freed while our tick
        // is still running — any FindAllOf / FindFirstOf call will
        // crash with an access violation (SEH, not catchable by C++).
        m_shuttingDown.store(true);
    }

    auto on_unreal_init() -> void override
    {
        m_core.Initialize(m_shuttingDown);

        // Key bindings must be registered on the CppUserModBase subclass
        register_keydown_event(Input::Key::F6, [this]() { m_core.OnKeyF6(); });
        register_keydown_event(Input::Key::F9, [this]() { m_core.OnKeyF9(); });
    }

    auto on_update() -> void override
    {
        m_core.Tick();
    }

private:
    TalosAP::ModCore   m_core;
    std::atomic<bool>  m_shuttingDown{false};
};

// ============================================================
// DLL Exports
// ============================================================
#define TALOS_AP_MOD_API __declspec(dllexport)
extern "C"
{
    TALOS_AP_MOD_API RC::CppUserModBase* start_mod()
    {
        return new TalosPrincipleArchipelagoMod();
    }

    TALOS_AP_MOD_API void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}
