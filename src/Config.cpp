#include "headers/Config.h"

#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace TalosAP {

// Convert wide string to narrow UTF-8 string
static std::string WideToNarrow(const std::wstring& wide)
{
    if (wide.empty()) return {};
    std::string result;
    result.reserve(wide.size());
    for (wchar_t ch : wide) {
        if (ch < 0x80) {
            result.push_back(static_cast<char>(ch));
        } else if (ch < 0x800) {
            result.push_back(static_cast<char>(0xC0 | (ch >> 6)));
            result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xE0 | (ch >> 12)));
            result.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
        }
    }
    return result;
}

// Convert narrow UTF-8 string to wide string
static std::wstring NarrowToWide(const std::string& narrow)
{
    if (narrow.empty()) return {};
    std::wstring result;
    result.reserve(narrow.size());
    for (size_t i = 0; i < narrow.size(); ) {
        unsigned char c = narrow[i];
        if (c < 0x80) {
            result.push_back(static_cast<wchar_t>(c));
            i += 1;
        } else if ((c >> 5) == 0x6) {
            wchar_t wc = (c & 0x1F) << 6;
            if (i + 1 < narrow.size()) wc |= (narrow[i + 1] & 0x3F);
            result.push_back(wc);
            i += 2;
        } else if ((c >> 4) == 0xE) {
            wchar_t wc = (c & 0x0F) << 12;
            if (i + 1 < narrow.size()) wc |= (narrow[i + 1] & 0x3F) << 6;
            if (i + 2 < narrow.size()) wc |= (narrow[i + 2] & 0x3F);
            result.push_back(wc);
            i += 3;
        } else {
            result.push_back(L'?');
            i += 1;
        }
    }
    return result;
}

void Config::SyncNarrowStrings()
{
    server_str    = WideToNarrow(server);
    slot_name_str = WideToNarrow(slot_name);
    password_str  = WideToNarrow(password);
    game_str      = WideToNarrow(game);
}

void Config::Load(const std::wstring& modDir)
{
    using namespace RC;

    // Build search paths
    std::vector<fs::path> searchPaths;
    if (!modDir.empty()) {
        searchPaths.push_back(fs::path(modDir) / L"config.json");
    }
    searchPaths.push_back(fs::path(L"Mods") / L"TalosPrincipleArchipelagoClient" / L"config.json");
    searchPaths.push_back(fs::path(L"config.json"));

    std::string fileContent;
    fs::path foundPath;

    for (const auto& path : searchPaths) {
        std::ifstream file(path);
        if (file.is_open()) {
            fileContent.assign(std::istreambuf_iterator<char>(file),
                               std::istreambuf_iterator<char>());
            foundPath = path;
            break;
        }
    }

    if (fileContent.empty()) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] config.json not found — using defaults\n"));
        SyncNarrowStrings();
        return;
    }

    try {
        auto j = json::parse(fileContent);

        if (j.contains("server") && j["server"].is_string()) {
            auto val = j["server"].get<std::string>();
            if (!val.empty()) server = NarrowToWide(val);
        }
        if (j.contains("slot_name") && j["slot_name"].is_string()) {
            auto val = j["slot_name"].get<std::string>();
            if (!val.empty()) slot_name = NarrowToWide(val);
        }
        if (j.contains("password") && j["password"].is_string()) {
            password = NarrowToWide(j["password"].get<std::string>());
        }
        if (j.contains("game") && j["game"].is_string()) {
            auto val = j["game"].get<std::string>();
            if (!val.empty()) game = NarrowToWide(val);
        }
        if (j.contains("offline_mode") && j["offline_mode"].is_string()) {
            auto val = j["offline_mode"].get<std::string>();
            offline_mode = (val == "true" || val == "1");
        }
        if (j.contains("death_link")) {
            if (j["death_link"].is_boolean()) {
                death_link = j["death_link"].get<bool>();
            } else if (j["death_link"].is_string()) {
                auto val = j["death_link"].get<std::string>();
                death_link = (val == "true" || val == "1");
            } else if (j["death_link"].is_number_integer()) {
                death_link = (j["death_link"].get<int>() != 0);
            }
        }

        // Helper lambda: parse a JSON value as bool (supports bool, string, int)
        auto parseBool = [&](const char* key, bool defaultVal) -> bool {
            if (!j.contains(key)) return defaultVal;
            const auto& v = j[key];
            if (v.is_boolean()) return v.get<bool>();
            if (v.is_string()) {
                auto s = v.get<std::string>();
                return (s == "true" || s == "1");
            }
            if (v.is_number_integer()) return v.get<int>() != 0;
            return defaultVal;
        };

        // Feature toggles
        // tetromino_handling is a nested object
        if (j.contains("tetromino_handling") && j["tetromino_handling"].is_object()) {
            const auto& th = j["tetromino_handling"];
            auto parseBoolObj = [&](const json& obj, const char* key, bool defaultVal) -> bool {
                if (!obj.contains(key)) return defaultVal;
                const auto& v = obj[key];
                if (v.is_boolean()) return v.get<bool>();
                if (v.is_string()) {
                    auto s = v.get<std::string>();
                    return (s == "true" || s == "1");
                }
                if (v.is_number_integer()) return v.get<int>() != 0;
                return defaultVal;
            };
            tetromino_handling.scan_for_new_tetrominos = parseBoolObj(th, "scan_for_new_tetrominos", true);
            tetromino_handling.enforce_visibility      = parseBoolObj(th, "enforce_visibility", true);
            tetromino_handling.player_proximity_pickup = parseBoolObj(th, "player_proximity_pickup", true);
            tetromino_handling.fence_opening           = parseBoolObj(th, "fence_opening", true);
        }

        enable_goal_detection         = parseBool("enable_goal_detection", true);
        enable_inventory_sync         = parseBool("enable_inventory_sync", true);
        enable_hud                    = parseBool("enable_hud", true);
        enable_debug_commands         = parseBool("enable_debug_commands", true);
        enable_save_hooks             = parseBool("enable_save_hooks", true);
        enable_level_transition_hooks = parseBool("enable_level_transition_hooks", true);
    }
    catch (const json::exception& e) {
        Output::send<LogLevel::Warning>(STR("[TalosAP] config.json parse error: {}\n"),
                                        NarrowToWide(e.what()));
    }

    SyncNarrowStrings();

    Output::send<LogLevel::Verbose>(STR("[TalosAP] Config loaded from {}\n"), foundPath.wstring());
    Output::send<LogLevel::Verbose>(STR("[TalosAP]   server    = {}\n"), server);
    Output::send<LogLevel::Verbose>(STR("[TalosAP]   slot_name = {}\n"), slot_name);
    Output::send<LogLevel::Verbose>(STR("[TalosAP]   password  = {}\n"),
                                    password.empty() ? L"(none)" : L"****");
    Output::send<LogLevel::Verbose>(STR("[TalosAP]   game      = {}\n"), game);
    if (offline_mode) {
        Output::send<LogLevel::Verbose>(STR("[TalosAP]   offline_mode = true\n"));
    }
    if (death_link) {
        Output::send<LogLevel::Verbose>(STR("[TalosAP]   death_link = true\n"));
    }

    // Log feature toggles (only log disabled ones to reduce noise)
    auto logToggle = [](const wchar_t* name, bool val) {
        if (!val) {
            Output::send<LogLevel::Warning>(STR("[TalosAP]   {} = DISABLED\n"), name);
        }
    };
    logToggle(STR("tetromino_handling.scan_for_new_tetrominos"), tetromino_handling.scan_for_new_tetrominos);
    logToggle(STR("tetromino_handling.enforce_visibility"), tetromino_handling.enforce_visibility);
    logToggle(STR("tetromino_handling.player_proximity_pickup"), tetromino_handling.player_proximity_pickup);
    logToggle(STR("tetromino_handling.fence_opening"), tetromino_handling.fence_opening);
    logToggle(STR("enable_goal_detection"), enable_goal_detection);
    logToggle(STR("enable_inventory_sync"), enable_inventory_sync);
    logToggle(STR("enable_hud"), enable_hud);
    logToggle(STR("enable_debug_commands"), enable_debug_commands);
    logToggle(STR("enable_save_hooks"), enable_save_hooks);
    logToggle(STR("enable_level_transition_hooks"), enable_level_transition_hooks);
}

} // namespace TalosAP
