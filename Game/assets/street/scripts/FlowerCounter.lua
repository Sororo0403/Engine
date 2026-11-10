local function valid_time(value)
    if type(value) == "number" and value >= 0 and value < math.huge then
        return value
    end
end

local function previous_clear_time()
    local saved = valid_time(game.streetBestClearTime)
    local full = valid_time(game.streetBestFullTime)
    local previous = saved
    if full and (not previous or full < previous) then
        previous = full
    end
    return previous
end

local function clear_time_line(state, tenths, previous)
    local saved = valid_time(game.streetBestClearTime)
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

local splitRecordVersion = "2:"

local function read_split_record(record)
    local a, b, c, total
    if type(record) == "string" and #record == 38 and string.sub(record, 1, 2) == splitRecordVersion then
        a, b = tonumber(string.sub(record, 3, 11)), tonumber(string.sub(record, 12, 20))
        c, total = tonumber(string.sub(record, 21, 29)), tonumber(string.sub(record, 30, 38))
    end
    if a and b and c and total and 0 <= a and a <= b and b <= c and c <= total then
        return {a, b, c, total}
    end
end

local function record_splits(state, finish, key)
    local previous = read_split_record(game[key])
    if not previous or finish < previous[4] then
        local splits = state.splits
        if splits.canal and splits.courtyard and splits.roof
            and splits.canal <= splits.courtyard and splits.courtyard <= splits.roof
            and splits.roof <= finish and finish <= 999999999 then
            game[key] = splitRecordVersion ..
                string.format("%09d%09d%09d%09d", splits.canal, splits.courtyard, splits.roof, finish)
        end
    end
    return previous
end

local function show_checkpoint(self, state, name, time)
    local notice = self:reference("checkpointNotice")
    if not notice then
        return
    end
    for index, checkpoint in ipairs(splitCheckpoints) do
        if checkpoint[1] == name then
            local text = string.format("%s  %.1f 秒", checkpoint[2], time / 10)
            if state.clearSplitRecord then
                local difference = time - state.clearSplitRecord[index]
                text = text .. string.format("  /  計測ベスト比 %+.1f 秒", difference / 10)
            end
            notice:set_text(text)
            return
        end
    end
end

local function split_line(state, finish, record)
    local parts, differences, previous = {}, {}, 0
    for i, checkpoint in ipairs(splitCheckpoints) do
        local key, label = table.unpack(checkpoint)
        local time = key == "finish" and finish or state.splits[key]
        local duration = time and previous and time >= previous and time - previous
        local value = duration and string.format("%.1f", duration / 10) or "—"
        parts[#parts + 1] = label .. " " .. value
        if record then
            local bestDuration = record[i] - (record[i - 1] or 0)
            local difference = duration and string.format("%+.1f", (duration - bestDuration) / 10) or "—"
            differences[#differences + 1] = label .. " " .. difference
        end
        previous = time
    end
    local line = "区間  " .. table.concat(parts, "  /  ") .. " 秒"
    if record then
        line = line .. "\n同じ計測地点の前ベスト比  " .. table.concat(differences, "  /  ") .. " 秒"
    end
    return line
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
    local unfinished = state.count < state.total
    local showRemaining = unfinished and (state.searching == true or state.hintHeld == true)
    resize_status_panel(self, state, showRemaining)
    local remaining
    if showRemaining then
        remaining = collection_summary(self, state)
        text = text .. "    " .. remaining
    elseif unfinished and not state.finishedTime and not state.completed then
        text = text .. "    残りの場所 ［H / View 長押し］"
    end
    game.streetRemainingFlowers = state.searching and remaining or nil
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
    start = function(self, state)
        state.splits = {}
        state.clearSplitRecord = read_split_record(game.streetBestClearSplits)
        game.journeyStreetTime = nil
        game.streetAllFlowers = state.count == state.total
        game.streetGoalWaiting = nil
        show_count(self, state)
    end,

    update = function(self, state, seconds, input)
        local action = input.actions.FlowerHints
        local playing = not state.finishedTime and not state.completed
        local hintHeld = playing and action ~= nil and action.down
        local searching = playing and (state.searching == true or game.streetGoalWaiting == true)
        if hintHeld ~= state.hintHeld or searching ~= state.searching then
            state.hintHeld = hintHeld
            state.searching = searching
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
            if not state.splits[name] then
                local time = math.floor(state.elapsed * 10)
                state.splits[name] = time
                show_checkpoint(self, state, name, time)
            end
        elseif name == "street_goal_reached" and not state.finishedTime then
            state.finishedTime = state.elapsed
            state.hintHeld = false
            state.searching = false
            show_count(self, state)
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
            game.journeyStreetTime = tenths
            game.journeyStreetFlowers = state.count
            local previous = previous_clear_time()
            local record = record_splits(state, tenths, "streetBestClearSplits")
            if state.count == state.total then
                record = record_splits(state, tenths, "streetBestFullSplits")
            end
            self:reference("result"):set_text(
                clear_time_line(state, tenths, previous) .. "\n" ..
                collection_summary(self, state) .. "\n" ..
                best_record(state, tenths) .. "\n" .. split_line(state, tenths, record))
            if not state.finishedTime then
                self:reference("hint"):set_active(false)
            end
            self:reference("clearSound"):play_audio()
            self:reference("resultPanel"):set_active(true)
        end
    end
}
