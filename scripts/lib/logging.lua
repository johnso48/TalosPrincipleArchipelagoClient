-- ============================================================
-- Logging utilities with level filtering
-- ============================================================

local LOG_LEVEL = {
    DEBUG = 1,
    INFO = 2,
    WARN = 3,
    ERROR = 4
}

local CURRENT_LOG_LEVEL = LOG_LEVEL.DEBUG

local function Log(level, levelName, msg)
    if level >= CURRENT_LOG_LEVEL then
        print(string.format("[ArchipelagoMod][%s] %s", levelName, msg))
    end
end

local function LogDebug(msg) Log(LOG_LEVEL.DEBUG, "DEBUG", msg) end
local function LogInfo(msg) Log(LOG_LEVEL.INFO, "INFO", msg) end
local function LogWarn(msg) Log(LOG_LEVEL.WARN, "WARN", msg) end
local function LogError(msg) Log(LOG_LEVEL.ERROR, "ERROR", msg) end

return {
    LOG_LEVEL = LOG_LEVEL,
    SetLevel = function(level) CURRENT_LOG_LEVEL = level end,
    LogDebug = LogDebug,
    LogInfo = LogInfo,
    LogWarn = LogWarn,
    LogWarning = LogWarn,  -- alias for consistency
    LogError = LogError
}
