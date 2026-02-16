-- ============================================================
-- HUD Notification Module
--
-- Displays on-screen messages using a UMG UserWidget + TextBlock.
-- Messages queue up and auto-dismiss after a configurable duration.
--
-- Usage:
--   local HUD = require("lib.hud")
--   HUD.Init()
--   HUD.ShowMessage("Player2 sent you Green J-Piece!", 5000)
--
-- The widget is automatically recreated after level transitions
-- when Init() is called again.
-- ============================================================

local Logging = require("lib.logging")

local M = {}

-- ============================================================
-- Configuration
-- ============================================================
local MAX_VISIBLE_LINES = 5       -- max messages shown at once
local DEFAULT_DURATION  = 5000    -- ms before a message fades
local TICK_INTERVAL     = 50     -- ms between cleanup ticks
local VIEWPORT_Z_ORDER  = 99     -- widget layer priority

-- ============================================================
-- Internal state
-- ============================================================
local widget    = nil   -- the UserWidget
local textBlock = nil   -- the TextBlock (root of widget tree)
local messages  = {}    -- array of {text=string, expireAt=number}
local pendingQueue = {} -- buffered messages waiting to be promoted
local tickCount = 0     -- monotonic counter incremented by TICK_INTERVAL
local initialized = false
local widgetCreationFailed = false  -- avoid retrying every tick
local creationPending = false       -- guard against async race

-- How many queued messages to promote into the visible list per tick.
-- 1 = one message per TICK_INTERVAL (200 ms). Increase for faster drain.
local DRAIN_PER_TICK = 1

-- ============================================================
-- Widget lifecycle
-- ============================================================

--- Destroy the current widget if it exists.
local function DestroyWidget()
    if widget then
        pcall(function()
            widget:RemoveFromViewport()
        end)
        widget = nil
        textBlock = nil
    end
end

--- Create (or recreate) the UMG widget.
--- Must be called from the game thread (ExecuteInGameThread).
local function CreateWidget()
    -- Guard: if another queued creation already ran, skip
    if widget then
        creationPending = false
        return
    end

    creationPending = false
    widgetCreationFailed = false

    local ok, err = pcall(function()
        -- Find class objects needed for construction
        local userWidgetClass  = StaticFindObject("/Script/UMG.UserWidget")
        local widgetTreeClass  = StaticFindObject("/Script/UMG.WidgetTree")
        local canvasPanelClass = StaticFindObject("/Script/UMG.CanvasPanel")
        local textBlockClass   = StaticFindObject("/Script/UMG.TextBlock")

        if not userWidgetClass or not widgetTreeClass or not textBlockClass then
            Logging.LogWarning("HUD: Could not find UMG classes — widget display unavailable")
            widgetCreationFailed = true
            return
        end

        -- We need an outer object for construction; use the game instance
        local gi = FindFirstOf("GameInstance")
        if not gi or not gi:IsValid() then
            Logging.LogWarning("HUD: No GameInstance found — deferring widget creation")
            return
        end

        -- Construct the UserWidget
        local w = StaticConstructObject(userWidgetClass, gi, FName("AP_NotificationWidget"))
        if not w or not w:IsValid() then
            Logging.LogWarning("HUD: Failed to construct UserWidget")
            widgetCreationFailed = true
            return
        end

        -- Construct the WidgetTree (required by UserWidget)
        local tree = StaticConstructObject(widgetTreeClass, w, FName("AP_NotifTree"))
        if not tree or not tree:IsValid() then
            Logging.LogWarning("HUD: Failed to construct WidgetTree")
            widgetCreationFailed = true
            return
        end
        w.WidgetTree = tree

        -- Construct a TextBlock
        local tb = StaticConstructObject(textBlockClass, tree, FName("AP_NotifText"))
        if not tb or not tb:IsValid() then
            Logging.LogWarning("HUD: Failed to construct TextBlock")
            widgetCreationFailed = true
            return
        end

        -- Try CanvasPanel layout for proper bottom-right anchoring
        local useCanvas = false
        if canvasPanelClass then
            local canvasOk, canvasErr = pcall(function()
                local canvas = StaticConstructObject(canvasPanelClass, tree, FName("AP_NotifCanvas"))
                if canvas and canvas:IsValid() then
                    tree.RootWidget = canvas
                    local slot = canvas:AddChildToCanvas(tb)
                    if slot then
                        -- Anchor to bottom-right corner of screen
                        pcall(function()
                            slot:SetAnchors({Minimum = {X = 1, Y = 1}, Maximum = {X = 1, Y = 1}})
                        end)
                        -- Pivot at bottom-right of the text block
                        pcall(function()
                            slot:SetAlignment({X = 1, Y = 1})
                        end)
                        -- Auto-size so the slot fits the text content
                        pcall(function()
                            slot:SetAutoSize(true)
                        end)
                        -- Offset inward from the corner (negative = towards center)
                        pcall(function()
                            slot:SetPosition({X = -30, Y = -50})
                        end)
                        useCanvas = true
                        Logging.LogInfo("HUD: Using CanvasPanel layout (bottom-right anchored)")
                    end
                end
            end)
            if not canvasOk then
                Logging.LogDebug("HUD: CanvasPanel layout failed: " .. tostring(canvasErr))
            end
        end

        -- Fallback: TextBlock as root with absolute positioning
        if not useCanvas then
            tree.RootWidget = tb
            Logging.LogInfo("HUD: Using fallback layout (absolute positioning)")
        end

        -- Style: empty initial text
        pcall(function() tb:SetText(FText("")) end)

        -- Font size — larger for readability
        pcall(function()
            local fontInfo = tb.Font
            if fontInfo then
                pcall(function() fontInfo.Size = 20 end)
            end
        end)

        -- Right-justify text so lines align to the right edge
        pcall(function() tb:SetJustification(2) end)  -- ETextJustify::Right = 2

        -- Strong drop shadow for contrast against any background
        pcall(function() tb:SetShadowOffset({X = 2.0, Y = 2.0}) end)
        pcall(function() tb:SetShadowColorAndOpacity({R = 0, G = 0, B = 0, A = 1.0}) end)

        -- Bright white text
        pcall(function() tb:SetColorAndOpacity({R = 1, G = 1, B = 1, A = 1}) end)

        -- Add to viewport
        w:AddToViewport(VIEWPORT_Z_ORDER)

        -- If no canvas layout, position via absolute coords (bottom-right-ish)
        if not useCanvas then
            pcall(function()
                -- Use alignment so the widget's bottom-right corner is the anchor
                w:SetAlignmentInViewport({X = 1, Y = 1})
                -- Large coords to push towards bottom-right
                -- (works well at 1080p+; the alignment handles the offset)
                w:SetPositionInViewport({X = 1890, Y = 1030}, false)
            end)
        end

        -- Commit to module state only after everything succeeded
        widget = w
        textBlock = tb

        Logging.LogInfo("HUD: Notification widget created successfully")
    end)

    if not ok then
        Logging.LogError("HUD: Widget creation error: " .. tostring(err))
        widgetCreationFailed = true
        widget = nil
        textBlock = nil
    end
end

-- ============================================================
-- Message management
-- ============================================================

--- Drain pending messages into the active list (rate-limited).
local function DrainPendingQueue()
    for _ = 1, DRAIN_PER_TICK do
        if #pendingQueue == 0 then break end
        local entry = table.remove(pendingQueue, 1)
        entry.expireAt = tickCount + math.ceil(entry.duration / TICK_INTERVAL)
        table.insert(messages, entry)
    end
end

--- Rebuild the displayed text from the active message queue.
local function RefreshDisplay()
    if not textBlock then return end

    -- Promote buffered messages first
    DrainPendingQueue()

    -- Remove expired messages
    local now = tickCount
    local active = {}
    for _, msg in ipairs(messages) do
        if msg.expireAt > now then
            table.insert(active, msg)
        end
    end
    messages = active

    -- Keep only the most recent MAX_VISIBLE_LINES
    local startIdx = math.max(1, #messages - MAX_VISIBLE_LINES + 1)
    local lines = {}
    for i = startIdx, #messages do
        table.insert(lines, messages[i].text)
    end

    local displayText = table.concat(lines, "\n")

    -- Update the text block
    local tb = textBlock
    pcall(function()
        if tb and tb:IsValid() then
            tb:SetText(FText(displayText))
        end
    end)
end

-- ============================================================
-- Request widget creation (deduped, async-safe)
-- ============================================================
local function RequestCreateWidget()
    if creationPending or widget then return end
    creationPending = true
    ExecuteInGameThread(function()
        CreateWidget()
    end)
end

-- ============================================================
-- Public API
-- ============================================================

--- Initialize or re-initialize the HUD widget.
--- Safe to call multiple times (e.g. after level transitions).
function M.Init()
    -- Invalidate the previous widget — it's likely stale after a level load
    widget = nil
    textBlock = nil
    widgetCreationFailed = false
    creationPending = false

    RequestCreateWidget()

    -- Start the cleanup/refresh loop only once
    if not initialized then
        initialized = true
        LoopAsync(TICK_INTERVAL, function()
            tickCount = tickCount + 1

            -- Prune expired messages and refresh
            if #messages > 0 or #pendingQueue > 0 then
                -- Validate that our widget is still alive
                if widget then
                    local alive = false
                    pcall(function() alive = widget:IsValid() end)
                    if not alive then
                        widget = nil
                        textBlock = nil
                    end
                end

                -- Try to recreate if needed
                if not widget and not widgetCreationFailed then
                    RequestCreateWidget()
                end

                RefreshDisplay()
            end

            return false  -- keep running
        end)
    end
end

--- Display a message on screen.
--- @param text string The message text
--- @param duration number|nil Duration in ms (default 5000)
function M.ShowMessage(text, duration)
    if not text or text == "" then return end

    duration = duration or DEFAULT_DURATION

    -- Buffer into the pending queue; the tick loop will drain it.
    table.insert(pendingQueue, {
        text = text,
        duration = duration,
        expireAt = 0  -- set when promoted
    })

    Logging.LogDebug(string.format("HUD: Buffered message (%d pending): %s", #pendingQueue, text))

    -- Ensure widget exists (don't force a RefreshDisplay here;
    -- the tick loop handles draining at a safe rate).
    if not widget and not widgetCreationFailed then
        RequestCreateWidget()
    end
end

--- Remove all messages and clear the display.
function M.Clear()
    messages = {}
    pendingQueue = {}
    if textBlock then
        ExecuteInGameThread(function()
            pcall(function()
                if textBlock and textBlock:IsValid() then
                    textBlock:SetText(FText(""))
                end
            end)
        end)
    end
end

--- Tear down the widget entirely.
function M.Shutdown()
    M.Clear()
    ExecuteInGameThread(function()
        DestroyWidget()
    end)
end

--- Check whether the widget is alive.
function M.IsReady()
    return widget ~= nil and not widgetCreationFailed
end

return M
