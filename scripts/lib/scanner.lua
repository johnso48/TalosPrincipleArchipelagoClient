-- ============================================================
-- Tetromino item scanning and tracking
-- ============================================================

local Logging = require("lib.logging")
local TetrominoUtils = require("lib.tetromino_utils")
local Collection -- lazy-loaded to avoid circular require

local DebugAnimLogged = false

local function getCollection()
    if not Collection then
        Collection = require("lib.collection")
    end
    return Collection
end

-- Scan all TetrominoItem actors and track their state.
-- Detects pickup events via bIsAnimating/bHidden transitions.
-- Does NOT force visibility here — that's handled by the visibility loop.
local function ScanTetrominoItems(state, onCollected)
    local items = FindAllOf("BP_TetrominoItem_C")
    if not items then return end
    
    local col = getCollection()
    
    -- Track which addresses we found this scan
    local foundThisScan = {}
    
    for _, item in ipairs(items) do
        if item and item:IsValid() then
            local addr = tostring(item:GetAddress())
            foundThisScan[addr] = true
            
            local tetrominoId = TetrominoUtils.GetTetrominoId(item)
            
            -- Check if this item is animating (being picked up)
            local isAnimating = nil
            local isHidden = nil
            
            pcall(function()
                isAnimating = item.bIsAnimating
                isHidden = item.bHidden
            end)
            
            -- If we haven't seen this item before, register it
            if not state.TrackedItems[addr] then
                local fullName = ""
                pcall(function() fullName = item:GetFullName() end)
                
                -- If item is already animating when we first see it, it's being picked up NOW
                local alreadyReported = false
                if isAnimating == true then
                    onCollected(tetrominoId)
                    alreadyReported = true
                end
                
                state.TrackedItems[addr] = {
                    id = tetrominoId,
                    name = fullName,
                    item = item,
                    wasAnimating = isAnimating,
                    wasHidden = isHidden,
                    reported = alreadyReported
                }
                
                Logging.LogInfo(string.format("Tracking: %s at address %s (anim=%s, hidden=%s)", 
                    tostring(tetrominoId), addr, tostring(isAnimating), tostring(isHidden)))
            else
                local info = state.TrackedItems[addr]
                
                -- Log first time we successfully read animation state (for debug)
                if not DebugAnimLogged and isAnimating ~= nil then
                    Logging.LogDebug(string.format("bIsAnimating readable, value=%s", tostring(isAnimating)))
                    DebugAnimLogged = true
                end
                
                -- Detect pickup: animation state changed (false -> true)
                if isAnimating == true and info.wasAnimating == false and not info.reported then
                    onCollected(info.id)
                    info.reported = true
                end
                
                -- Also detect pickup via hidden state change (false -> true)
                if isHidden == true and info.wasHidden == false and not info.reported then
                    onCollected(info.id)
                    info.reported = true
                end
                
                -- Reset reported flag if the item has become visible again
                -- (e.g. after EnforceCollectionState removed it from CollectedTetrominos
                -- and the visibility loop restored it). This allows re-pickup detection.
                if info.reported and tetrominoId and col.ShouldBeCollectable(tetrominoId) then
                    if isAnimating == false and isHidden == false then
                        info.reported = false
                    end
                end
                
                info.wasAnimating = isAnimating
                info.wasHidden = isHidden
            end
        end
    end
    
    -- Clean up items that are no longer in the level
    for addr, _ in pairs(state.TrackedItems) do
        if not foundThisScan[addr] then
            state.TrackedItems[addr] = nil
        end
    end
end

return {
    ScanTetrominoItems = ScanTetrominoItems
}
