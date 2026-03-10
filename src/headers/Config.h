#pragma once

#include <string>

namespace TalosAP {

struct Config {
    std::wstring server    = L"archipelago.gg:38281";
    std::wstring slot_name = L"Player1";
    std::wstring password  = L"";
    std::wstring game      = L"The Talos Principle Reawakened";
    bool offline_mode      = false;
    bool death_link        = false;

    // Feature toggles — set to false in config.json to disable individual
    // subsystems for crash-isolation testing.  All default to true.
    struct TetrominoHandling {
        bool scan_for_new_tetrominos  = true;
        bool enforce_visibility       = true;
        bool player_proximity_pickup  = true;
        bool fence_opening            = true;
    } tetromino_handling;

    bool enable_goal_detection      = true;
    bool enable_inventory_sync      = true;
    bool enable_hud                 = true;
    bool enable_debug_commands      = true;
    bool enable_save_hooks          = true;
    bool enable_level_transition_hooks = true;

    // Narrow-string versions for apclientpp (which uses std::string)
    std::string server_str;
    std::string slot_name_str;
    std::string password_str;
    std::string game_str;

    /// Load configuration from config.json located relative to the mod DLL.
    /// Falls back to defaults if the file is not found or malformed.
    void Load(const std::wstring& modDir);

private:
    /// Synchronize narrow-string copies from wide-string fields.
    void SyncNarrowStrings();
};

} // namespace TalosAP
