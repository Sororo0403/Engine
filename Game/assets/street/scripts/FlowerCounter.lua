local function best_record(state, seconds)
    local bestFlowers = game.streetBestFlowers
    if type(bestFlowers) ~= "number" or bestFlowers < 0 or bestFlowers > state.total then
        bestFlowers = 0
    end
    local bestTime = game.streetBestFullTime
    if type(bestTime) ~= "number" or bestTime < 0 or bestTime >= math.huge then
        bestTime = nil
    end
    local newRecord = state.count > bestFlowers
    if newRecord then
        bestFlowers = state.count
        game.streetBestFlowers = bestFlowers
    end
    if state.count == state.total then
        local tenths = math.floor(seconds * 10)
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
    return newRecord and "新記録！  " .. line or line
end

local function collection_summary(self, state)
    if state.count == state.total then
        return "街じゅうの花を見つけた！"
    end

    local remaining = {}
    for _, area in ipairs(state.flowerAreas or {}) do
        for _, reference in ipairs(area.flowers) do
            local flower = self:reference(reference)
            if flower and flower:is_active() then
                remaining[#remaining + 1] = area.label
                break
            end
        end
    end
    if #remaining > 0 then
        return "まだ花がある場所： " .. table.concat(remaining, "・")
    end
    return "まだ見つけていない花のかけらがあるよ"
end

local pickupHighlightSeconds = 0.55

local function show_pickup_highlight(self, state, amount)
    local _, _, _, alpha = self:get_text_color()
    local r, g, b = table.unpack(state.normalColor)
    self:set_text_color(r + (1 - r) * amount, g + (0.82 - g) * amount,
        b + (0.30 - b) * amount, alpha)
end

return {
    update = function(self, state, seconds)
        if not state.finishedTime and not state.completed and not game.streetDeathActive then
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

        if name == "street_goal_reached" and not state.finishedTime then
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
            local sparkles = self:reference("pickupSparkles")
            if sparkles then
                sparkles:emit_particles(16)
            end
            self:set_text("花のかけら " .. state.count .. " / " .. state.total)
            state.normalColor = state.normalColor or {self:get_text_color()}
            state.pickupHighlight = pickupHighlightSeconds
            show_pickup_highlight(self, state, 1)
        elseif name == "street_clear" then
            state.completed = true
            local seconds = state.finishedTime or state.elapsed
            self:reference("result"):set_text(
                "花のかけら " .. state.count .. " / " .. state.total ..
                string.format("    タイム %.1f 秒\n", math.floor(seconds * 10) / 10) ..
                collection_summary(self, state) .. "\n" ..
                best_record(state, seconds))
            if not state.finishedTime then
                self:reference("hint"):set_active(false)
            end
            self:reference("clearSound"):play_audio()
            self:reference("resultPanel"):set_active(true)
        end
    end
}
