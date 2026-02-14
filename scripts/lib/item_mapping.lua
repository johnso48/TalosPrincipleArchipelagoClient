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
    "MT1",  "DJ3",  "DJ1",  "DJ2",  "DZ1",
    "ML1",  "DI1",  "ML2",  "DZ2",  "DL1",
    "MT2",  "DZ3",  "MT3",  "NL1",  "MZ1",
    "MT4",  "MZ2",  "MT5",  "DT2",  "DL2",
    "DT1",  "NZ1",  "DI2",  "NL3",  "NZ2",
    "NL2",  "DZ4",  "NO1",  "DL3",  "DT3",
    "NT1",  "NL4",  "ML3",  "MZ3",  "MS1",
    "MT6",  "MT7",  "MS2",  "MZ4",  "NL5",
    "MT8",  "MT9",  "NT2",  "MJ1",  "NL6",
    "NL8",  "DJ4",  "DT4",  "NT4",  "NL7",
    "NT3",  "DJ5",  "NL9",  "NZ3",  "NS1",
    "NI1",  "MT10", "ML4",  "NI2",  "MI1",
    "NJ1",  "NI3",  "MO1",  "NT5",  "NI4",
    "NJ2",  "NZ4",  "NS2",  "NZ5",  "NO2",
    "NT6",  "NT7",  "NO3",  "NZ6",  "NJ3",
    "NT9",  "NS3",  "NI5",  "NT8",  "NO4",
    "NO5",  "NT10", "NI6",  "NO6",  "NS4",
    "NJ4",  "NT12", "NT11", "NL10", "NO7",
    -- Secret / Alternate ending
    "EL1",  "ES1",  "ES3",  "EL2",  "EL3",
    "ES2",  "EL4",  "EO1",  "ES4",
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
