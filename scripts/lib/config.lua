-- ============================================================
-- Configuration loader
-- Reads config.json from the mod's scripts directory.
-- Falls back to defaults if the file is missing or malformed.
-- ============================================================

local Logging = require("lib.logging")

local M = {}

-- Defaults
M.server    = "archipelago.gg:38281"
M.slot_name = "Player1"
M.password  = ""
M.game      = "The Talos Principle Reawakened"

-- ============================================================
-- Minimal JSON parser (handles flat objects with string values)
-- ============================================================
local function parse_json_flat(str)
    local result = {}
    -- Match "key": "value" or "key": number patterns
    for key, value in str:gmatch('"([^"]+)"%s*:%s*"([^"]*)"') do
        result[key] = value
    end
    return result
end

-- ============================================================
-- Load config from file
-- ============================================================
function M.Load()
    -- Determine the scripts directory from the module path
    local scriptDir = nil
    pcall(function()
        -- UE4SS sets the working directory; config.json is next to main.lua
        local info = debug.getinfo(1, "S")
        if info and info.source then
            local src = info.source:gsub("^@", "")
            scriptDir = src:match("(.+)[/\\][^/\\]+$")
        end
    end)

    -- Fallback: try relative path
    local paths = {}
    if scriptDir then
        table.insert(paths, scriptDir .. "\\config.json")
        table.insert(paths, scriptDir .. "/config.json")
    end
    table.insert(paths, "config.json")

    local configContent = nil
    local configPath = nil

    for _, path in ipairs(paths) do
        local f = io.open(path, "r")
        if f then
            configContent = f:read("*a")
            f:close()
            configPath = path
            break
        end
    end

    if not configContent then
        Logging.LogWarning("config.json not found — using defaults. Create config.json with server/slot_name/password.")
        return M
    end

    local parsed = parse_json_flat(configContent)

    if parsed.server and parsed.server ~= "" then
        M.server = parsed.server
    end
    if parsed.slot_name and parsed.slot_name ~= "" then
        M.slot_name = parsed.slot_name
    end
    if parsed.password then
        M.password = parsed.password
    end
    if parsed.game and parsed.game ~= "" then
        M.game = parsed.game
    end

    Logging.LogInfo(string.format("Config loaded from %s", configPath))
    Logging.LogInfo(string.format("  server    = %s", M.server))
    Logging.LogInfo(string.format("  slot_name = %s", M.slot_name))
    Logging.LogInfo(string.format("  password  = %s", M.password ~= "" and "****" or "(none)"))
    Logging.LogInfo(string.format("  game      = %s", M.game))

    return M
end

return M
