-- ============================================================
-- Tetromino visibility and collision management
-- ============================================================

local Logging = require("lib.logging")

-- Force a tetromino item to be visible and have working collision/overlap.
-- Call this in a fast loop for items that should be collectable.
local function SetTetrominoVisible(item)
    if not item or not item:IsValid() then 
        return false
    end
    
    local success = false
    
    -- Set actor-level visibility and collision
    pcall(function()
        item.bHidden = false
        item.bActorEnableCollision = true
        success = true
    end)
    
    -- Set mesh component visibility and collision
    pcall(function()
        if item.TetrominoMesh then
            item.TetrominoMesh.bVisible = true
            item.TetrominoMesh.bHiddenInGame = false
            item.TetrominoMesh.CollisionEnabled = 3
            item.TetrominoMesh.bGenerateOverlapEvents = true
            success = true
        end
    end)
    
    -- Set root component visibility and collision
    pcall(function()
        if item.Root then
            item.Root.bHiddenInGame = false
            item.Root.bVisible = true
            item.Root.CollisionEnabled = 3
            item.Root.bGenerateOverlapEvents = true
            success = true
        end
    end)
    
    -- CRITICAL: Enable the Capsule component collision.
    -- The Capsule (UCapsuleComponent) is the actual overlap trigger that detects
    -- the player walking into the tetromino. When HideTetromino() is called on
    -- collection, it disables this component's collision, preventing re-pickup.
    pcall(function()
        if item.Capsule and item.Capsule:IsValid() then
            -- 1 = QueryOnly (sufficient for overlap detection)
            -- bGenerateOverlapEvents must be true for OnBeginOverlap to fire
            item.Capsule.CollisionEnabled = 1
            item.Capsule.bGenerateOverlapEvents = true
            success = true
        end
    end)
    
    return success
end

-- Force a tetromino item to be hidden with no collision.
-- Call this for items that have been checked or granted and should not be in-world.
local function SetTetrominoHidden(item)
    if not item or not item:IsValid() then
        return false
    end

    -- Let the game's own HideTetromino handle it cleanly
    local ok = pcall(function()
        item:HideTetromino()
    end)

    if not ok then
        -- Fallback: manually hide
        pcall(function()
            item.bHidden = true
        end)
        pcall(function()
            if item.Capsule and item.Capsule:IsValid() then
                item.Capsule.CollisionEnabled = 0
                item.Capsule.bGenerateOverlapEvents = false
            end
        end)
    end

    return true
end

-- Call the game's own UnhideTetromino() to properly restore all state.
-- Heavier than SetTetrominoVisible — use once per item, not in a tight loop.
local function UnhideTetrominoFull(item)
    if not item or not item:IsValid() then
        return false
    end
    
    local ok, err = pcall(function()
        item:UnhideTetromino()
    end)
    
    if not ok then
        Logging.LogDebug(string.format("UnhideTetromino() call failed: %s", tostring(err)))
        return SetTetrominoVisible(item)
    end
    
    -- Also ensure Capsule collision is enabled (UnhideTetromino may not do this)
    pcall(function()
        if item.Capsule and item.Capsule:IsValid() then
            item.Capsule.CollisionEnabled = 1
            item.Capsule.bGenerateOverlapEvents = true
        end
    end)
    
    return true
end

return {
    SetTetrominoVisible = SetTetrominoVisible,
    SetTetrominoHidden = SetTetrominoHidden,
    UnhideTetrominoFull = UnhideTetrominoFull
}
