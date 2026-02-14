-- ============================================================
-- Tetromino inventory management
-- ============================================================

local Logging = require("lib.logging")
local TetrominoUtils = require("lib.tetromino_utils")
local Progress = require("lib.progress")

-- Check if a tetromino is already in the inventory
local function IsTetrominoInInventory(state, tetrominoId)
    if not state.CurrentProgress or not state.CurrentProgress:IsValid() then
        return false
    end
    
    local found = false
    local tmap = nil
    local ok = pcall(function()
        tmap = state.CurrentProgress.CollectedTetrominos
    end)
    
    if ok and tmap then
        pcall(function()
            tmap:ForEach(function(keyParam, valueParam)
                local keyStr = nil
                pcall(function()
                    local got = keyParam:get()
                    if got then
                        keyStr = got:ToString()
                    end
                end)
                if keyStr == tetrominoId then
                    found = true
                end
            end)
        end)
    end
    
    return found
end

-- Refresh the UI - try various approaches
local function RefreshTetrominoUI(state)
    Logging.LogDebug("Attempting UI refresh...")
    
    -- Find the specific tetromino item that was just added (if we know its ID)
    local targetTetrominoItem = nil
    if state.LastAddedTetrominoId then
        local items = FindAllOf("BP_TetrominoItem_C")
        if items then
            for _, item in ipairs(items) do
                if item and item:IsValid() then
                    local itemId = TetrominoUtils.GetTetrominoId(item)
                    if itemId == state.LastAddedTetrominoId then
                        targetTetrominoItem = item
                        Logging.LogDebug(string.format("Found tetromino item for %s", state.LastAddedTetrominoId))
                        break
                    end
                end
            end
        end
    end
    
    -- APPROACH 1: Call OnScriptTetrominoCollected_BP on arrangers with the specific item
    local arrangers = FindAllOf("BP_ScriptArranger_C")
    if not arrangers then
        arrangers = FindAllOf("ScriptArranger")
    end
    if arrangers and targetTetrominoItem then
        Logging.LogDebug(string.format("Found %d arrangers, notifying them of collected tetromino", #arrangers))
        for _, arranger in ipairs(arrangers) do
            if arranger and arranger:IsValid() then
                local ok = pcall(function()
                    arranger:OnScriptTetrominoCollected_BP(targetTetrominoItem)
                end)
                if ok then
                    Logging.LogDebug("Called OnScriptTetrominoCollected_BP on arranger")
                end
            end
        end
    end
    
    -- APPROACH 2: Try calling PerformWidgetUpdates on the specific item
    if targetTetrominoItem then
        pcall(function()
            targetTetrominoItem:PerformWidgetUpdates()
            Logging.LogDebug("Called PerformWidgetUpdates on target item")
        end)
    end
    
    -- APPROACH 3: Find TalosHUD and call UpdateExplorationMode
    local talosHuds = FindAllOf("TalosHUD")
    if talosHuds then
        Logging.LogDebug(string.format("Found %d TalosHUD instances", #talosHuds))
        for _, hud in ipairs(talosHuds) do
            if hud and hud:IsValid() then
                local fullName = ""
                pcall(function() fullName = hud:GetFullName() end)
                
                -- Only process huds that look like real instances (not CDOs)
                if fullName:find("Transient") or fullName:find("Engine") then
                    Logging.LogDebug(string.format("Processing TalosHUD: %s", fullName))
                    pcall(function()
                        hud:UpdateExplorationMode()
                    end)
                    pcall(function()
                        local arrangerInfo = hud.ArrangerInfo
                        if arrangerInfo then
                            arrangerInfo:UpdateInventory()
                        end
                    end)
                end
            end
        end
    end
    
    Logging.LogDebug("UI refresh attempts complete")
end

-- Add a tetromino to the player's inventory
local function AddTetrominoToInventory(state, tetrominoId)
    -- Force refresh to get the real active progress object
    if not Progress.FindProgressObject(state, true) then
        Logging.LogWarn("Cannot add tetromino - no progress object")
        return false
    end
    
    -- Log which progress object we're using
    local progAddr = "unknown"
    pcall(function() progAddr = tostring(state.CurrentProgress:GetAddress()) end)
    local timePlayed = "unknown"
    pcall(function() timePlayed = tostring(state.CurrentProgress.TimePlayed) end)
    Logging.LogDebug(string.format("Using progress object at %s, timePlayed=%s", progAddr, timePlayed))
    
    local targetType, targetShape, targetNum = TetrominoUtils.ParseTetrominoId(tetrominoId)
    if not targetType then
        Logging.LogWarn(string.format("Cannot add tetromino - invalid ID: %s", tostring(tetrominoId)))
        return false
    end
    
    -- Check if already in inventory
    if IsTetrominoInInventory(state, tetrominoId) then
        Logging.LogInfo(string.format("%s is already in inventory", tetrominoId))
        return true
    end
    
    Logging.LogInfo(string.format("Adding tetromino %s (Type=%d, Shape=%d, Num=%d) to inventory...", 
        tetrominoId, targetType, targetShape, targetNum))
    
    -- Add to the CollectedTetrominos TMap
    local tmap = nil
    local ok, err = pcall(function()
        tmap = state.CurrentProgress.CollectedTetrominos
    end)
    
    if not ok or not tmap then
        Logging.LogWarn(string.format("Could not access CollectedTetrominos: %s", tostring(err)))
        return false
    end
    
    -- Value is false to indicate collected but not yet used in a puzzle
    local addOk, addErr = pcall(function()
        tmap:Add(tetrominoId, false)
    end)
    
    if not addOk then
        Logging.LogWarn(string.format("TMap:Add failed: %s", tostring(addErr)))
        return false
    end
    
    Logging.LogInfo(string.format("Added %s to CollectedTetrominos TMap", tetrominoId))
    
    -- Track for UI refresh
    state.LastAddedTetrominoId = tetrominoId
    
    -- Refresh UI with multiple approaches
    RefreshTetrominoUI(state)
    
    return true
end

return {
    IsTetrominoInInventory = IsTetrominoInInventory,
    AddTetrominoToInventory = AddTetrominoToInventory
}
