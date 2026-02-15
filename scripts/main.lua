-- ============================================================
-- Archipelago Mod for The Talos Principle Reawakened
-- Main initialization and coordination
-- ============================================================

-- Load modules
local Logging = require("lib.logging")
local TetrominoUtils = require("lib.tetromino_utils")
local Visibility = require("lib.visibility")
local Progress = require("lib.progress")
local Scanner = require("lib.scanner")
local Inventory = require("lib.inventory")
local Collection = require("lib.collection")
local Config = require("lib.config")
local ItemMapping = require("lib.item_mapping")
local APClient = require("lib.ap_client")

Logging.LogInfo("==============================================")
Logging.LogInfo("Archipelago Mod Loading...")
Logging.LogInfo("==============================================")

-- ============================================================
-- Load configuration
-- ============================================================
Config.Load()

-- ============================================================
-- Shared state
-- ============================================================
local State = {
    CurrentProgress = nil,
    TrackedItems = {},
    CollectedThisSession = {},
    LevelTransitionCooldown = 0,
    LastAddedTetrominoId = nil
}

-- External callback for Archipelago integration
-- Wired to APClient.SendLocationCheck below
OnLocationCheckedCallback = nil

-- ============================================================
-- Initialize Archipelago client
-- ============================================================
local apAvailable = false
if Config.offline_mode then
    Logging.LogInfo("Offline mode enabled — AP communication disabled")
    Logging.LogInfo("Items can be granted/revoked via debug keybinds (F5/F8)")
    Logging.LogInfo("Pickups will log item names and location IDs without sending to server")
    
    -- Wire a local-only callback that logs and self-grants
    OnLocationCheckedCallback = function(tetrominoId)
        local locId = ItemMapping.GetLocationId(tetrominoId)
        Logging.LogInfo(string.format("[OFFLINE] Location check: %s (location_id=%s)", 
            tetrominoId, tostring(locId)))
        -- In offline mode, grant the item directly so the player can progress
        Collection.GrantItem(State, tetrominoId)
        Logging.LogInfo(string.format("[OFFLINE] Auto-granted: %s", tetrominoId))
    end
else
    apAvailable = APClient.Init(Config, State, Collection, ItemMapping)
    if apAvailable then
        -- Wire the location checked callback to the AP client
        OnLocationCheckedCallback = function(tetrominoId)
            APClient.SendLocationCheck(tetrominoId)
        end
        
        -- Connect (will happen asynchronously via poll)
        APClient.Connect()
        Logging.LogInfo("AP client initialized — connection will be established via poll()")
    else
        Logging.LogWarning("AP client not available — running in local-only mode")
        Logging.LogWarning("Items can still be granted/revoked via debug keybinds (F5/F8)")
    end
end

-- ============================================================
-- Handle tetromino physical pickup event (location checked)
-- This fires when the player walks into a tetromino in-world.
-- It does NOT mean the item is in their inventory — Archipelago
-- decides that separately via Collection.GrantItem().
-- ============================================================
local function OnTetrominoCollected(tetrominoId)
    Logging.LogInfo(string.format("*** LOCATION CHECKED: %s ***", tostring(tetrominoId)))
    
    -- Mark this location as checked so the item stays hidden
    Collection.MarkLocationChecked(tetrominoId)
    State.CollectedThisSession[tetrominoId] = true
    
    -- Clear visibility cache so the fast loop re-evaluates this item
    if VisibilityApplied then
        VisibilityApplied[tetrominoId] = nil
    end
    
    -- Delayed UI refresh: the enforce loop will remove the item from TMap
    -- but the HUD won't update until we explicitly tell it to.
    LoopAsync(2000, function()
        Collection.RefreshUI()
        return true -- run once
    end)
    
    -- Notify Archipelago client
    if OnLocationCheckedCallback then
        local ok, err = pcall(function()
            OnLocationCheckedCallback(tetrominoId)
        end)
        if not ok then
            Logging.LogError(string.format("Location callback error: %s", tostring(err)))
        end
    end
end

-- ============================================================
-- Hook player spawn - this is when we should refresh progress
-- ============================================================
RegisterHook("/Script/Engine.PlayerController:ClientRestart", function(Context)
    Logging.LogDebug("Player spawned - refreshing progress and clearing tracked items")
    
    -- Force refresh the progress object since we may have loaded a different save
    State.CurrentProgress = nil
    Progress.FindProgressObject(State, true)
    
    State.TrackedItems = {}
    State.LevelTransitionCooldown = 50
    
    -- Clear visibility cache — actors are new after level transition
    VisibilityApplied = {}
    
    -- Log which save we're now tracking
    if State.CurrentProgress and State.CurrentProgress:IsValid() then
        local timePlayed = 0
        local level = "?"
        pcall(function() timePlayed = State.CurrentProgress.TimePlayed end)
        pcall(function() level = State.CurrentProgress.LastPlayedPersistentLevel end)
        Logging.LogInfo(string.format("Now tracking save with timePlayed=%.0f, level=%s", timePlayed, tostring(level)))
    end
    
    -- Schedule delayed enforcement + scan to ensure everything is fully loaded
    LoopAsync(2000, function()
        -- Enforce collection state before scanning so items are collectable
        Collection.EnforceCollectionState(State)
        Scanner.ScanTetrominoItems(State, OnTetrominoCollected)
        return true -- stop after one execution
    end)
end)

-- ============================================================
-- Hook save game set — fires when the game instance receives
-- a new or loaded save game object. This is the authoritative
-- moment to re-acquire the progress object.
-- ============================================================
RegisterHook("/Script/Talos.TalosGameInstance:SetTalosSaveGameInstance", function(Context)
    Logging.LogInfo("Save game instance set on TalosGameInstance — refreshing progress")
    -- Pause loops during save game transition
    State.LevelTransitionCooldown = 50
    VisibilityApplied = {}
    -- Delay slightly to let the engine finish wiring up
    LoopAsync(200, function()
        State.CurrentProgress = nil
        Progress.FindProgressObject(State, true)
        State.TrackedItems = {}
        State.CollectedThisSession = {}

        if State.CurrentProgress then
            Logging.LogInfo("=== SAVE FILE DUMP (on save game set) ===")
            Progress.DumpSaveFileContents(State)
        end
        return true
    end)
end)

-- ============================================================
-- Hook save game reload (Continue / Load from menu)
-- ============================================================
RegisterHook("/Script/Talos.TalosGameInstance:ReloadSaveGame", function(Context)
    Logging.LogInfo("ReloadSaveGame called — will re-acquire progress on player spawn")
    State.CurrentProgress = nil
    State.TrackedItems = {}
    State.LevelTransitionCooldown = 50
    VisibilityApplied = {}
end)

-- ============================================================
-- Periodic polling loop — scan every 100ms for detection and tracking
-- Also enforces collection state (removes non-granted items from
-- CollectedTetrominos so they remain collectable).
-- ============================================================
local LoopCount = 0
LoopAsync(100, function()
    LoopCount = LoopCount + 1

    -- Poll the Archipelago client (handles network I/O)
    APClient.Poll()

    -- Decrement level transition cooldown — skip enforcement while
    -- the world is (un)loading actors to avoid null dereferences.
    if State.LevelTransitionCooldown > 0 then
        State.LevelTransitionCooldown = State.LevelTransitionCooldown - 1
        if State.LevelTransitionCooldown == 0 then
            Logging.LogInfo("Level transition cooldown expired — resuming enforcement")
        end
        return false
    end

    local ok, err = pcall(function()
        if not State.CurrentProgress or not State.CurrentProgress:IsValid() then
            Progress.FindProgressObject(State)
        end

        if State.CurrentProgress and State.CurrentProgress:IsValid() then
            -- Enforce: keep CollectedTetrominos in sync with Archipelago grants
            Collection.EnforceCollectionState(State)
            
            -- Scan for pickup events
            Scanner.ScanTetrominoItems(State, OnTetrominoCollected)
        end
    end)

    if not ok then
        Logging.LogError(string.format("Loop error: %s", tostring(err)))
    end

    return false
end)

-- ============================================================
-- Fast visibility loop — every 10ms manage item visibility
-- Items that should be collectable: force visible + collision
-- Also temporarily removes them from TMap so the game's
-- IsTetrominoCollected check returns false, allowing pickup.
-- The enforce loop (100ms) re-adds granted items to TMap, so
-- they remain usable in arrangers most of the time.
--
-- IMPORTANT: We track which items have already been set to their
-- target state to avoid re-applying visibility every tick.
-- Constantly toggling rendering properties causes DLSS temporal
-- accumulation to corrupt, producing flickering artifacts
-- (especially visible during rain/particle effects).
-- ============================================================
local VisibilityApplied = {}  -- id -> "visible" | "hidden"

LoopAsync(10, function()
    -- Skip during level transitions to avoid accessing destroyed actors
    if State.LevelTransitionCooldown > 0 then
        return false
    end

    local ok, err = pcall(function()
        local items = FindAllOf("BP_TetrominoItem_C")
        if items then
            for _, item in ipairs(items) do
                if item and item:IsValid() then
                    local id = TetrominoUtils.GetTetrominoId(item)
                    if id then
                        if Collection.ShouldBeCollectable(id) then
                            -- Remove from TMap so IsTetrominoCollected returns false
                            -- This allows OnBeginOverlap to proceed with pickup
                            if State.CurrentProgress and State.CurrentProgress:IsValid() then
                                pcall(function()
                                    State.CurrentProgress.CollectedTetrominos:Remove(id)
                                end)
                            end
                            -- Only touch rendering state once
                            if VisibilityApplied[id] ~= "visible" then
                                Visibility.SetTetrominoVisible(item)
                                VisibilityApplied[id] = "visible"
                            end
                        elseif Collection.IsLocationChecked(id) and not Collection.IsGranted(id) then
                            -- Location was checked but item wasn't granted to us
                            -- (AP sent it to another player). Hide the actor so
                            -- it doesn't reappear, but do NOT add to TMap.
                            if VisibilityApplied[id] ~= "hidden" then
                                Visibility.SetTetrominoHidden(item)
                                VisibilityApplied[id] = "hidden"
                            end
                        end
                    end
                end
            end
        end
    end)

    if not ok then
        Logging.LogError(string.format("Visibility loop error: %s", tostring(err)))
    end

    return false
end)

-- ============================================================
-- Rain suppression loop — disable weather particles if configured
-- The UDW (Ultra Dynamic Weather) rain system uses Niagara particles
-- that can cause visual flickering with DLSS temporal accumulation.
-- ============================================================
if Config.disable_rain then
    Logging.LogInfo("Rain suppression enabled — weather particles will be disabled")
    LoopAsync(1000, function()
        pcall(function()
            local rainActors = FindAllOf("UDW_Rain_C")
            if rainActors then
                for _, rain in ipairs(rainActors) do
                    if rain and rain:IsValid() then
                        -- Kill spawn rate so no new particles emit
                        pcall(function() rain.RainSpawnRate = 0 end)
                        -- Disable splash and fog particles
                        pcall(function() rain["Enable Fog Particles"] = false end)
                        pcall(function() rain["Fog Particles Active"] = false end)
                        pcall(function() rain["Splash Frequency"] = 0 end)
                        -- Deactivate the Niagara particle component
                        pcall(function()
                            if rain["Rain Particles"] and rain["Rain Particles"]:IsValid() then
                                rain["Rain Particles"]:Deactivate()
                            end
                        end)
                    end
                end
            end
            -- Also suppress snow if present (same DLSS flickering issue)
            local snowActors = FindAllOf("UDW_Snow_C")
            if snowActors then
                for _, snow in ipairs(snowActors) do
                    if snow and snow:IsValid() then
                        pcall(function() snow.RainSpawnRate = 0 end)
                        pcall(function() snow["Enable Fog Particles"] = false end)
                        pcall(function() snow["Fog Particles Active"] = false end)
                        pcall(function() snow["Splash Frequency"] = 0 end)
                        -- Deactivate the Niagara particle component
                        pcall(function()
                            if snow["Rain Particles"] and snow["Rain Particles"]:IsValid() then
                                snow["Rain Particles"]:Deactivate()
                            end
                        end)
                    end
                end
            end
        end)
        return false -- keep running
    end)
end

-- ============================================================
-- Debug keybinds
-- ============================================================

-- F5: Grant DJ3 via Archipelago (simulates receiving item from multiworld)
RegisterKeyBind(Key.F5, function()
    Logging.LogInfo("=== F5: Granting DJ3 (Archipelago grant) ===")
    Progress.FindProgressObject(State, true)
    Collection.GrantItem(State, "DJ3")
    Collection.DumpState()
    Progress.DumpSaveFileContents(State)
end)

-- F6: Dump full state (collection state + inventory + progress)
RegisterKeyBind(Key.F6, function()
    Logging.LogInfo("=== F6: Full state dump ===")
    Progress.FindProgressObject(State, true)
    
    -- Collection module state
    Collection.DumpState()
    
    -- All progress objects
    local allProgress = FindAllOf("BP_TalosProgress_C")
    if allProgress then
        Logging.LogInfo(string.format("Found %d BP_TalosProgress_C objects in memory:", #allProgress))
        for i, prog in ipairs(allProgress) do
            if prog and prog:IsValid() then
                local addr = "unknown"
                pcall(function() addr = tostring(prog:GetAddress()) end)
                local slot = "?"
                pcall(function() slot = tostring(prog.TalosProgressSlot) end)
                local timePlayed = "?"
                pcall(function() timePlayed = tostring(prog.TimePlayed) end)
                local count = "?"
                pcall(function()
                    local c = 0
                    prog.CollectedTetrominos:ForEach(function() c = c + 1 end)
                    count = tostring(c)
                end)
                Logging.LogInfo(string.format("  [%d] addr=%s slot=%s time=%s tetrominos=%s", i, addr, slot, timePlayed, count))
            end
        end
    end
    
    Progress.DumpSaveFileContents(State)
end)

-- F7: Inspect Capsule collision on all items + collection state
RegisterKeyBind(Key.F7, function()
    Logging.LogInfo("=== F7: Item state inspection ===")
    
    local items = FindAllOf("BP_TetrominoItem_C")
    if not items then
        Logging.LogInfo("No tetromino items found")
        return
    end
    
    Logging.LogInfo(string.format("Found %d tetromino items", #items))
    
    for i, item in ipairs(items) do
        if item and item:IsValid() then
            local id = TetrominoUtils.GetTetrominoId(item)
            local collectable = id and Collection.ShouldBeCollectable(id) or false
            local granted = id and Collection.IsGranted(id) or false
            local checked = id and Collection.IsLocationChecked(id) or false
            
            Logging.LogInfo(string.format("\n--- %s --- collectable=%s granted=%s checked=%s",
                id, tostring(collectable), tostring(granted), tostring(checked)))
            
            pcall(function()
                Logging.LogInfo(string.format("  bHidden=%s bActorEnableCollision=%s bIsAnimating=%s",
                    tostring(item.bHidden), tostring(item.bActorEnableCollision), tostring(item.bIsAnimating)))
            end)
            
            pcall(function()
                if item.Capsule and item.Capsule:IsValid() then
                    Logging.LogInfo(string.format("  Capsule: CollisionEnabled=%s bGenerateOverlapEvents=%s",
                        tostring(item.Capsule.CollisionEnabled), tostring(item.Capsule.bGenerateOverlapEvents)))
                else
                    Logging.LogInfo("  Capsule: nil or invalid")
                end
            end)
            
            if i >= 5 then
                Logging.LogInfo("\n... (showing first 5 items only)")
                break
            end
        end
    end
end)

-- F8: Revoke DJ3 (simulates losing item, makes it re-appear and re-collectable)
RegisterKeyBind(Key.F8, function()
    Logging.LogInfo("=== F8: Revoking DJ3 + resetting location ===")
    Progress.FindProgressObject(State, true)
    Collection.RevokeItem(State, "DJ3")
    Collection.ResetLocation("DJ3")
    Collection.DumpState()
end)

-- F9: Grant Connector items (MT1, ML1, MT2)
RegisterKeyBind(Key.F9, function()
    Logging.LogInfo("=== F9: Granting Connector items ===")
    Progress.FindProgressObject(State, true)
    local connectorItems = {"MT1", "ML1", "MT2"}
    for _, id in ipairs(connectorItems) do
        Collection.GrantItem(State, id)
    end
    Logging.LogInfo("Connector unlocked (MT1, ML1, MT2)")
    Collection.DumpState()
end)

-- F10: Grant Hexahedron items (MT4, MT5, ML2) + Connector
RegisterKeyBind(Key.F10, function()
    Logging.LogInfo("=== F10: Granting Hexahedron + Connector items ===")
    Progress.FindProgressObject(State, true)
    local hexItems = {"MT1", "ML1", "MT2", "MT4", "MT5", "ML2"}
    for _, id in ipairs(hexItems) do
        Collection.GrantItem(State, id)
    end
    Logging.LogInfo("Hexahedron + Connector unlocked")
    Collection.DumpState()
end)

-- F11: Grant all items needed for World A/B gates + Fans + Playback
RegisterKeyBind(Key.F11, function()
    Logging.LogInfo("=== F11: Granting World A/B gate items + Fans + Playback ===")
    Progress.FindProgressObject(State, true)
    local worldABItems = {
        -- Connector
        "MT1", "ML1", "MT2",
        -- Hexahedron
        "MT4", "MT5", "ML2",
        -- World A1 gate (2 Green J, 1 Green Z)
        "DJ1", "DJ2", "DZ1",
        -- World A gate (1 Green J, 1 Green I, 1 Green L, 1 Green Z)
        "DJ3", "DI1", "DL1", "DZ2",
        -- World B gate (1 Green L, 2 Green T, 1 Green Z, 1 Green I)
        "DL2", "DT1", "DT2", "DZ3", "DI2",
        -- Fans (2 Yellow T, 1 Yellow L, 1 Yellow Z, 1 Yellow S)
        "MT6", "MT7", "ML3", "MZ3", "MS1",
        -- Playback (2 Yellow T, 1 Yellow J, 1 Yellow S, 1 Yellow Z)
        "MT8", "MT9", "MJ1", "MS2", "MZ4"
    }
    for _, id in ipairs(worldABItems) do
        Collection.GrantItem(State, id)
    end
    Logging.LogInfo("World A/B gates + Fans + Playback unlocked")
    Collection.DumpState()
end)

-- F12: Grant ALL gate items (Connector + Hexahedron + Fans + Playback + all gates through World C)
RegisterKeyBind(Key.F12, function()
    Logging.LogInfo("=== F12: Granting ALL gate items (full unlock) ===")
    Progress.FindProgressObject(State, true)
    local allGateItems = {
        -- Connector
        "MT1", "ML1", "MT2",
        -- Hexahedron
        "MT4", "MT5", "ML2",
        -- Fans
        "MT6", "MT7", "ML3", "MZ3", "MS1",
        -- Playback
        "MT8", "MT9", "MJ1", "MS2", "MZ4",
        -- World A1 gate
        "DJ1", "DJ2", "DZ1",
        -- World A gate
        "DJ3", "DI1", "DL1", "DZ2",
        -- World B gate
        "DL2", "DT1", "DT2", "DZ3", "DI2",
        -- World C gate (2 Green T, 2 Green J, 1 Green L, 1 Green Z)
        "DT3", "DT4", "DJ4", "DJ5", "DL3", "DZ4",
        -- Additional A-world items mentioned
        "MZ1", "MZ2", "NZ1", "NL1", "NL2", "NL3", "NZ2", "NL4", "NO1", "NT1",
        -- Additional B-world items mentioned
        "NL5", "NL6", "NT3", "NT4",
        -- World B7 Golden pieces (platform unlock)
        "MO1", "MI1",
        -- World B7 Red pieces
        "NJ1", "NI3",
        -- World C1 Red pieces
        "NZ4", "NJ2", "NI4", "NT5",
        -- World C2 Red pieces
        "NZ5", "NO2", "NT6", "NS2",
        -- World C3 Red pieces
        "NJ3", "NO3", "NZ6", "NT7",
        -- World C4 Red pieces
        "NT8", "NI5", "NS3", "NT9",
        -- World C5 Red pieces
        "NI6", "NO4", "NO5", "NT10",
        -- World C6 Red pieces
        "NS4", "NJ4", "NO6",
        -- World C7 Red pieces
        "NT11", "NO7", "NT12", "NL10"
    }
    for _, id in ipairs(allGateItems) do
        Collection.GrantItem(State, id)
    end
    Logging.LogInfo("ALL gates + tools unlocked (through World C)")
    Collection.DumpState()
end)

-- ============================================================
-- Export for external Archipelago client access
-- ============================================================
return {
    State = State,
    Collection = Collection,
    OnTetrominoCollected = OnTetrominoCollected
}
