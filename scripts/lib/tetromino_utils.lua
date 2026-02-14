-- ============================================================
-- Tetromino ID utilities - formatting, parsing, finding
-- ============================================================

local TypeToLetter = {
    [1] = "D",   -- Door
    [2] = "M",   -- Mechanic
    [4] = "N",   -- Nexus
    [8] = "S",   -- Secret
    [16] = "E",  -- AlternativeEnding
    [32] = "A",  -- Arcade
    [64] = "H",  -- Help
}

local ShapeToLetter = {
    [1] = "I",
    [2] = "J",
    [4] = "L",
    [8] = "O",
    [16] = "S",
    [32] = "T",
    [64] = "Z",
}

local LetterToType = {
    ["D"] = 1,   -- Door
    ["M"] = 2,   -- Mechanic
    ["N"] = 4,   -- Nexus
    ["S"] = 8,   -- Secret
    ["E"] = 16,  -- AlternativeEnding
    ["A"] = 32,  -- Arcade
    ["H"] = 64,  -- Help
}

local LetterToShape = {
    ["I"] = 1,
    ["J"] = 2,
    ["L"] = 4,
    ["O"] = 8,
    ["S"] = 16,
    ["T"] = 32,
    ["Z"] = 64,
}

-- Format tetromino ID from type/shape/number
local function FormatTetrominoId(typeVal, shapeVal, numVal)
    local t = tonumber(typeVal) or 0
    local s = tonumber(shapeVal) or 0
    local n = tonumber(numVal) or 0
    
    local typeLetter = TypeToLetter[t] or string.format("T%d", t)
    local shapeLetter = ShapeToLetter[s] or string.format("S%d", s)
    
    return string.format("%s%s%d", typeLetter, shapeLetter, n)
end

-- Extract tetromino ID from an instance
local function GetTetrominoId(item)
    local id = nil
    local rawType, rawShape, rawNum = nil, nil, nil
    
    pcall(function()
        local info = item.InstanceInfo
        if info then
            rawType = info.Type
            rawShape = info.Shape
            rawNum = info.Number
            
            id = FormatTetrominoId(rawType, rawShape, rawNum)
        end
    end)
    
    return id, rawType, rawShape, rawNum
end

-- Parse tetromino ID string back to Type/Shape/Number
local function ParseTetrominoId(id)
    if not id or #id < 3 then
        return nil, nil, nil
    end
    
    local typeLetter = id:sub(1, 1)
    local shapeLetter = id:sub(2, 2)
    local numStr = id:sub(3)
    
    local typeVal = LetterToType[typeLetter]
    local shapeVal = LetterToShape[shapeLetter]
    local numVal = tonumber(numStr)
    
    if not typeVal or not shapeVal or not numVal then
        return nil, nil, nil
    end
    
    return typeVal, shapeVal, numVal
end

-- Find tetromino item by ID
local function FindTetrominoItemById(tetrominoId)
    local targetType, targetShape, targetNum = ParseTetrominoId(tetrominoId)
    if not targetType then return nil end
    
    local items = FindAllOf("BP_TetrominoItem_C")
    if not items then return nil end
    
    for _, item in ipairs(items) do
        if item and item:IsValid() then
            local id = GetTetrominoId(item)
            if id == tetrominoId then
                return item
            end
        end
    end
    return nil
end

return {
    FormatTetrominoId = FormatTetrominoId,
    GetTetrominoId = GetTetrominoId,
    ParseTetrominoId = ParseTetrominoId,
    FindTetrominoItemById = FindTetrominoItemById
}
