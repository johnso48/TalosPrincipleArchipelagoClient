-- ============================================================
-- Progress object finding and save file operations
--
-- The game's save structure:
--   UTalosGameInstance
--     └─ SaveGameInstance (UTalosSaveGame)
--          └─ ProgressHistory (TArray<UTalosProgress*>)
--               └─ [slot index] → UTalosProgress (== BP_TalosProgress_C)
--
-- Authoritative accessors:
--   UTalosProgress::Get(WorldContext) → current active progress
--   UTalosSaveGame::Get(WorldContext) → current save game
--   UTalosSaveGame::GetProgress(slot) → progress for specific slot
-- ============================================================

local Logging = require("lib.logging")

-- ============================================================
-- Primary strategy: use the game's own static accessor
-- UTalosProgress::Get(WorldContext) returns the active progress.
-- ============================================================
local function GetProgressViaStaticAccessor()
    -- We need any valid world-context UObject. The player controller works.
    local worldCtx = nil

    -- Try player controller (always exists while in-game)
    pcall(function()
        local pc = FindFirstOf("PlayerController")
        if pc and pc:IsValid() then
            worldCtx = pc
        end
    end)

    -- Fallback: game instance itself
    if not worldCtx then
        pcall(function()
            local gi = FindFirstOf("TalosGameInstance")
            if not gi or not gi:IsValid() then
                gi = FindFirstOf("ScriptTalosGameInstance")
            end
            if gi and gi:IsValid() then
                worldCtx = gi
            end
        end)
    end

    if not worldCtx then return nil end

    -- Call the static UTalosProgress::Get(WorldContext)
    -- In UE4SS Lua, static UFunctions on a CDO are called as methods.
    local progress = nil
    pcall(function()
        local cdo = StaticFindObject("/Script/Talos.Default__TalosProgress")
        if cdo and cdo:IsValid() then
            progress = cdo:Get(worldCtx)
        end
    end)

    if progress and pcall(function() return progress:IsValid() end) then
        -- Sanity check: can we read CollectedTetrominos?
        local canRead = false
        pcall(function()
            progress.CollectedTetrominos:ForEach(function()
                canRead = true
                return true
            end)
        end)
        if canRead then
            return progress
        end
    end

    return nil
end

-- ============================================================
-- Secondary strategy: go through SaveGame → GetProgress(slot)
-- ============================================================
local function GetProgressViaSaveGame()
    local worldCtx = nil
    pcall(function()
        local pc = FindFirstOf("PlayerController")
        if pc and pc:IsValid() then worldCtx = pc end
    end)
    if not worldCtx then
        pcall(function()
            local gi = FindFirstOf("TalosGameInstance")
            if gi and gi:IsValid() then worldCtx = gi end
        end)
    end
    if not worldCtx then return nil end

    -- Get the save game via its static accessor
    local saveGame = nil
    pcall(function()
        local cdo = StaticFindObject("/Script/Talos.Default__TalosSaveGame")
        if cdo and cdo:IsValid() then
            saveGame = cdo:Get(worldCtx)
        end
    end)

    if not saveGame then
        -- Fallback: get via game instance property
        pcall(function()
            local gi = FindFirstOf("TalosGameInstance")
            if not gi or not gi:IsValid() then
                gi = FindFirstOf("ScriptTalosGameInstance")
            end
            if gi and gi:IsValid() then
                saveGame = gi.SaveGameInstance
            end
        end)
    end

    if not saveGame then return nil end

    -- Get the current slot number from the save subsystem
    local slotNum = 0
    pcall(function()
        local subsystem = FindFirstOf("TalosSaveSubsystem")
        if subsystem and subsystem:IsValid() then
            slotNum = subsystem:GetSaveGameCurrentSlotNumber()
        end
    end)

    -- Get progress for that slot
    local progress = nil
    pcall(function()
        progress = saveGame:GetProgress(slotNum)
    end)

    if progress then
        local valid = false
        pcall(function() valid = progress:IsValid() end)
        if valid then
            return progress
        end
    end

    return nil
end

-- ============================================================
-- Tertiary strategy: find from BP_TalosCharacter_C
-- The character often has a .Progress reference to the active one.
-- ============================================================
local function GetProgressViaCharacter()
    local char = FindFirstOf("BP_TalosCharacter_C")
    if not char then return nil end

    local progress = nil
    pcall(function()
        if char:IsValid() then
            progress = char.Progress
        end
    end)

    if progress then
        local valid = false
        pcall(function() valid = progress:IsValid() end)
        if valid then return progress end
    end

    return nil
end

-- ============================================================
-- Public API
-- ============================================================

local function FindProgressObject(state, forceRefresh)
    if not forceRefresh and state.CurrentProgress then
        local valid = false
        pcall(function() valid = state.CurrentProgress:IsValid() end)
        if valid then return true end
    end

    -- Try strategies in order of reliability
    local progress = GetProgressViaStaticAccessor()

    if not progress then
        progress = GetProgressViaSaveGame()
    end

    if not progress then
        progress = GetProgressViaCharacter()
    end

    if progress then
        local addr = "?"
        pcall(function() addr = tostring(progress:GetAddress()) end)
        local slot = "?"
        pcall(function() slot = tostring(progress.TalosProgressSlot) end)
        local timePlayed = "?"
        pcall(function() timePlayed = string.format("%.0f", progress.TimePlayed) end)

        -- Only log if the object actually changed
        local changed = (state.CurrentProgress ~= progress)
        state.CurrentProgress = progress

        if changed then
            Logging.LogInfo(string.format("Progress object acquired: addr=%s slot=%s timePlayed=%s", addr, slot, timePlayed))
        end
        return true
    end

    state.CurrentProgress = nil
    return false
end

-- Print current save file tetromino contents
local function DumpSaveFileContents(state)
    -- Ensure we have a valid progress object (won't overwrite if already valid)
    if not FindProgressObject(state) then
        Logging.LogWarn("Cannot dump save - no progress object")
        return
    end
    
    Logging.LogInfo("=== SAVE FILE CONTENTS ===")
    
    -- Show info about current progress object
    local currentSlot = "?"
    local currentLevel = "?"
    local timePlayed = "?"
    pcall(function() currentSlot = state.CurrentProgress.TalosProgressSlot end)
    pcall(function() currentLevel = state.CurrentProgress.LastPlayedPersistentLevel end)
    pcall(function() timePlayed = state.CurrentProgress.TimePlayed end)
    
    local timeStr = "?"
    if type(timePlayed) == "number" then
        timeStr = string.format("%d:%02d", math.floor(timePlayed / 60), math.floor(timePlayed % 60))
    end
    Logging.LogInfo(string.format("Current Progress: slot=%s, level=%s, timePlayed=%s", 
        tostring(currentSlot), tostring(currentLevel), timeStr))
    
    -- Try to get the TMap
    local tmap = nil
    local tmapOk, tmapErr = pcall(function()
        tmap = state.CurrentProgress.CollectedTetrominos
    end)
    
    if not tmapOk or not tmap then
        Logging.LogWarn(string.format("Could not access CollectedTetrominos: %s", tostring(tmapErr)))
        return
    end
    
    -- Count entries using ForEach
    local entries = {}
    local forEachOk, forEachErr = pcall(function()
        tmap:ForEach(function(keyParam, valueParam)
            local keyStr = "?"
            pcall(function()
                local unwrapped = UnwrapParam(keyParam)
                if unwrapped then keyStr = tostring(unwrapped) end
            end)
            if keyStr == "?" or keyStr:find("FString") or keyStr:find("RemoteUnrealParam") then
                pcall(function()
                    local got = keyParam:get()
                    if got then
                        local ts = got:ToString()
                        if ts then keyStr = ts end
                    end
                end)
            end
            table.insert(entries, keyStr)
        end)
    end)
    
    if not forEachOk then
        Logging.LogWarn(string.format("ForEach failed: %s", tostring(forEachErr)))
    end
    
    Logging.LogInfo(string.format("CollectedTetrominos has %d entries:", #entries))
    
    -- Sort for readability
    table.sort(entries)
    for i, key in ipairs(entries) do
        Logging.LogInfo(string.format("  [%d] %s", i, key))
    end
    
    Logging.LogInfo("=== END SAVE FILE CONTENTS ===")
end

return {
    FindProgressObject = FindProgressObject,
    DumpSaveFileContents = DumpSaveFileContents
}
