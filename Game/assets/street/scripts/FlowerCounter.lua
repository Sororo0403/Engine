local function valid_time(value)
    if type(value) == "number" and value >= 0 and value < math.huge then
        return value
    end
end

local function clear_time_line(state, tenths)
    local saved = valid_time(game.streetBestClearTime)
    local full = valid_time(game.streetBestFullTime)
    local previous = saved
    if full and (not previous or full < previous) then
        previous = full
    end
    local improved = not previous or tenths < previous
    local best = improved and tenths or previous
    if saved ~= best then
        game.streetBestClearTime = best
    end
    return string.format("花のかけら %d / %d    タイム %.1f 秒    クリア最速 %.1f 秒%s",
        state.count, state.total, tenths / 10, best / 10, improved and "（更新）" or "")
end

local function best_record(state, tenths)
    local bestFlowers = game.streetBestFlowers
    if type(bestFlowers) ~= "number" or bestFlowers < 0 or bestFlowers > state.total then
        bestFlowers = 0
    end
    local bestTime = valid_time(game.streetBestFullTime)
    local timeDifference
    local newRecord = state.count > bestFlowers
    if newRecord then
        bestFlowers = state.count
        game.streetBestFlowers = bestFlowers
    end
    if state.count == state.total then
        if bestTime then
            timeDifference = tenths - bestTime
        end
        if not bestTime or tenths < bestTime then
            bestTime = tenths
            game.streetBestFullTime = bestTime
            newRecord = true
        end
    end
    local line = "最高収集 " .. math.floor(bestFlowers) .. " / " .. state.total
    if bestTime then
        line = line .. string.format("    全収集ベスト %.1f 秒", bestTime / 10)
    end
    if timeDifference == 0 then
        line = line .. "（同タイム）"
    elseif timeDifference then
        line = line .. string.format("（前ベスト比 %+.1f 秒）", timeDifference / 10)
    end
    return newRecord and "新記録！  " .. line or line
end

local splitCheckpoints = {
    {"canal", "運河"},
    {"courtyard", "中庭"},
    {"roof", "屋上"},
    {"finish", "時計台"}
}

local function split_line(state, finish)
    local parts, previous = {}, 0
    for _, checkpoint in ipairs(splitCheckpoints) do
        local key, label = table.unpack(checkpoint)
        local time = key == "finish" and finish or state.splits[key]
        local value = time and previous and time >= previous
            and string.format("%.1f", (time - previous) / 10) or "—"
        parts[#parts + 1] = label .. " " .. value
        previous = time
    end
    return "区間  " .. table.concat(parts, "  /  ") .. " 秒"
end

local function collection_summary(self, state)
    if state.count == state.total then
        return "街じゅうの花を見つけた！"
    end

    local remaining = {}
    for _, area in ipairs(state.flowerAreas or {}) do
        local count = 0
        for _, reference in ipairs(area.flowers) do
            local flower = self:reference(reference)
            if flower and flower:is_active() then
                count = count + 1
            end
        end
        if count > 0 then
            remaining[#remaining + 1] = area.label .. " " .. count .. "個"
        end
    end
    if #remaining > 0 then
        return "残り： " .. table.concat(remaining, " ・ ")
    end
    return "まだ見つけていない花のかけらがあるよ"
end

local function resize_status_panel(self, state, expanded)
    if expanded == (state.statusPanelWidth ~= nil) then
        return
    end
    local panel = self:reference("statusPanel")
    if not panel then
        return
    end
    local width, height = panel:get_ui_size()
    if expanded then
        state.statusPanelWidth = width
        local panelX = panel:get_ui_position()
        local textX = self:get_ui_position()
        local textWidth = self:get_ui_size()
        panel:set_ui_size(math.max(width, textX - panelX + textWidth + 12), height)
    else
        panel:set_ui_size(state.statusPanelWidth, height)
        state.statusPanelWidth = nil
    end
end

local function show_count(self, state)
    local text = "花のかけら " .. state.count .. " / " .. state.total
    local showRemaining = state.searching == true and state.count < state.total
    resize_status_panel(self, state, showRemaining)
    if showRemaining then
        game.streetRemainingFlowers = collection_summary(self, state)
        text = text .. "    " .. game.streetRemainingFlowers
    else
        game.streetRemainingFlowers = nil
    end
    self:set_text(text)
end

local pickupHighlightSeconds = 0.55

local function show_pickup_highlight(self, state, amount)
    local _, _, _, alpha = self:get_text_color()
    local r, g, b = table.unpack(state.normalColor)
    self:set_text_color(r + (1 - r) * amount, g + (0.82 - g) * amount,
        b + (0.30 - b) * amount, alpha)
end

return {
    start = function(_, state)
        state.splits = {}
        game.streetAllFlowers = state.count == state.total
        game.streetGoalWaiting = nil
        game.streetRemainingFlowers = nil
    end,

    update = function(self, state, seconds)
        if game.streetGoalWaiting and state.count < state.total and not state.searching then
            state.searching = true
            show_count(self, state)
        end
        if not state.finishedTime and not state.completed then
            state.elapsed = state.elapsed + seconds
        end
        if state.pickupHighlight then
            state.pickupHighlight = math.max(0, state.pickupHighlight - seconds)
            show_pickup_highlight(self, state, state.pickupHighlight / pickupHighlightSeconds)
            if state.pickupHighlight == 0 then
                state.pickupHighlight = nil
            end
        end
    end,

    message = function(self, state, name)
        if state.completed then
            return
        end

        if not state.finishedTime and (name == "canal" or name == "courtyard" or name == "roof") then
            state.splits[name] = state.splits[name] or math.floor(state.elapsed * 10)
        elseif name == "street_goal_reached" and not state.finishedTime then
            state.finishedTime = state.elapsed
            self:reference("hint"):set_active(false)
            if state.count == state.total then
                local celebration = self:reference("clearCelebration")
                if celebration then
                    celebration:emit_particles(64)
                end
            end
        elseif name == "collect_flower" and not state.finishedTime and state.count < state.total then
            state.count = state.count + 1
            game.streetAllFlowers = state.count == state.total
            local sparkles = self:reference("pickupSparkles")
            if sparkles then
                sparkles:emit_particles(16)
            end
            show_count(self, state)
            state.normalColor = state.normalColor or {self:get_text_color()}
            state.pickupHighlight = pickupHighlightSeconds
            show_pickup_highlight(self, state, 1)
        elseif name == "street_clear" then
            state.completed = true
            local seconds = state.finishedTime or state.elapsed
            local tenths = math.floor(seconds * 10)
            self:reference("result"):set_text(
                clear_time_line(state, tenths) .. "\n" ..
                collection_summary(self, state) .. "\n" ..
                best_record(state, tenths) .. "\n" .. split_line(state, tenths))
            if not state.finishedTime then
                self:reference("hint"):set_active(false)
            end
            self:reference("clearSound"):play_audio()
            self:reference("resultPanel"):set_active(true)
        end
    end
}
