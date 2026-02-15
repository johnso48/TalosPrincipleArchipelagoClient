-- ============================================================
-- Archipelago Item & Location ID Mappings
--
-- Maps between in-game tetromino IDs (e.g. "DJ3", "MT1") and
-- Archipelago's numeric item/location IDs.
--
-- NOTE: These IDs must match the AP world definition for
-- "The Talos Principle Reawakened". The base item ID and
-- location ID should be assigned during world registration.
-- These are PLACEHOLDER values that need to be updated to
-- match the actual AP world once it's created.
-- ============================================================

local Logging = require("lib.logging")

local M = {}

-- ============================================================
-- Base ID offsets (must match AP world definition)
-- AP convention: each game gets a range. These are placeholders.
-- ============================================================
M.BASE_ITEM_ID     = 0x540000  -- 5505024
M.BASE_LOCATION_ID = 0x540000  -- 5505024

-- ============================================================
-- All tetrominoes in the game (from BotPuzzleDatabase.csv)
-- ============================================================
local ALL_TETROMINOES = {
    -- World A1 (7)
    "DJ3",  "MT1",  "DZ1",  "DJ2",  "DJ1",  "ML1",  "DI1",
    -- World A2 (3)
    "ML2",  "DL1",  "DZ2",
    -- World A3 (4)
    "MT2",  "DZ3",  "NL1",  "MT3",
    -- World A4 (4)
    "MZ1",  "MZ2",  "MT4",  "MT5",
    -- World A5 (5)
    "NZ1",  "DI2",  "DT1",  "DT2",  "DL2",
    -- World A6 (4)
    "DZ4",  "NL2",  "NL3",  "NZ2",
    -- World A7 (5)
    "NL4",  "DL3",  "NT1",  "NO1",  "DT3",
    -- World B1 (5)
    "ML3",  "MZ3",  "MS1",  "MT6",  "MT7",
    -- World B2 (4)
    "NL5",  "MS2",  "MT8",  "MZ4",
    -- World B3 (3)
    "MT9",  "MJ1",  "NL6",
    -- World B4 (6)
    "NT3",  "NT4",  "DT4",  "DJ4",  "NL7",  "NL8",
    -- World B5 (5)
    "NI1",  "NL9",  "NS1",  "DJ5",  "NZ3",
    -- World B6 (3)
    "NI2",  "MT10", "ML4",
    -- World B7 (4)
    "NJ1",  "NI3",  "MO1",  "MI1",
    -- World C1 (4)
    "NZ4",  "NJ2",  "NI4",  "NT5",
    -- World C2 (4)
    "NZ5",  "NO2",  "NT6",  "NS2",
    -- World C3 (4)
    "NJ3",  "NO3",  "NZ6",  "NT7",
    -- World C4 (4)
    "NT8",  "NI5",  "NS3",  "NT9",
    -- World C5 (4)
    "NI6",  "NO4",  "NO5",  "NT10",
    -- World C6 (3)
    "NS4",  "NJ4",  "NO6",
    -- World C7 (4)
    "NT11", "NO7",  "NT12", "NL10",
}

-- ============================================================
-- Stars (collectible stars, mapped separately)
-- Format: ** followed by number from BotPuzzleDatabase.csv
-- We map these as additional items/locations
-- ============================================================
local ALL_STARS = {
    { "SCentralArea_Chapter", "Star5"  },
    { "SCloud_1_02",          "Star2"  },
    { "S015",                 "Star1"  },
    { "SCloud_1_03",          "Star3"  },
    { "S202b",                "Star4"  },
    { "S201",                 "Star7"  },
    { "S244",                 "Star6"  },
    { "SCloud_1_06",          "Star8"  },
    { "S209",                 "Star9"  },
    { "S205",                 "Star10" },
    { "S213",                 "Star11" },
    { "S300a",                "Star12" },
    { "SCloud_2_04",          "Star24" },
    { "S215",                 "Star13" },
    { "SCloud_2_05",          "Star14" },
    { "S301",                 "Star16" },
    { "SCloud_2_07",          "Star15" },
    { "SCloud_3_01",          "Star17" },
    { "SIslands_01",          "Star26" },
    { "SLevel05_Elevator",    "Star25" },
    { "S403",                 "Star18" },
    { "S318",                 "Star19" },
    { "S408",                 "Star21" },
    { "S405",                 "Star20" },
    { "S328",                 "Star23" },
    { "S404",                 "Star27" },
    { "S309",                 "Star22" },
    { "SNexus",               "Star28" },
    { "S234",                 "Star29" },
    { "S308",                 "Star30" },
}

-- ============================================================
-- Build lookup tables
-- ============================================================

-- tetrominoId -> AP item ID
M.ItemNameToId = {}
-- AP item ID -> tetrominoId
M.ItemIdToName = {}
-- tetrominoId -> AP location ID (same mapping — the location is where the item sits)
M.LocationNameToId = {}
-- AP location ID -> tetrominoId
M.LocationIdToName = {}
-- puzzleCode -> tetrominoId
M.PuzzleToTetromino = {}

local function BuildTables()
    local idx = 0

    -- Tetrominoes: each one is both an item (grant) and a location (check)
    for _, tetId in ipairs(ALL_TETROMINOES) do
        local itemId = M.BASE_ITEM_ID + idx
        local locId  = M.BASE_LOCATION_ID + idx

        M.ItemNameToId[tetId]   = itemId
        M.ItemIdToName[itemId]  = tetId
        M.LocationNameToId[tetId] = locId
        M.LocationIdToName[locId] = tetId

        idx = idx + 1
    end

    -- Stars
    for _, entry in ipairs(ALL_STARS) do
        local puzzleCode = entry[1]
        local starId = entry[2]

        local itemId = M.BASE_ITEM_ID + idx
        local locId  = M.BASE_LOCATION_ID + idx

        M.ItemNameToId[starId]   = itemId
        M.ItemIdToName[itemId]   = starId
        M.LocationNameToId[starId] = locId
        M.LocationIdToName[locId]  = starId
        M.PuzzleToTetromino[puzzleCode] = starId

        idx = idx + 1
    end

    Logging.LogInfo(string.format("Item mappings built: %d items, %d locations", idx, idx))
end

-- ============================================================
-- Query helpers
-- ============================================================

function M.GetItemId(tetrominoId)
    return M.ItemNameToId[tetrominoId]
end

function M.GetItemName(apItemId)
    return M.ItemIdToName[apItemId]
end

function M.GetLocationId(tetrominoId)
    return M.LocationNameToId[tetrominoId]
end

function M.GetLocationName(apLocationId)
    return M.LocationIdToName[apLocationId]
end

-- Get all location IDs as a flat list
function M.GetAllLocationIds()
    local ids = {}
    for id, _ in pairs(M.LocationIdToName) do
        table.insert(ids, id)
    end
    table.sort(ids)
    return ids
end

-- Get all item IDs as a flat list
function M.GetAllItemIds()
    local ids = {}
    for id, _ in pairs(M.ItemIdToName) do
        table.insert(ids, id)
    end
    table.sort(ids)
    return ids
end

-- Initialize
BuildTables()

return M
