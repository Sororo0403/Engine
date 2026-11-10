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

return {
    update = function(self, state, seconds)
        if not state.finishedTime and not state.completed and not game.streetDeathActive then
            state.elapsed = state.elapsed + seconds
        end
    end,

    message = function(self, state, name)
        if state.completed then
            return
        end

        if name == "street_goal_reached" and not state.finishedTime then
            state.finishedTime = state.elapsed
            self:reference("hint"):set_active(false)
            self:reference("clearSound"):play_audio()
        elseif name == "collect_flower" and not state.finishedTime and state.count < state.total then
            state.count = state.count + 1
            local sparkles = self:reference("pickupSparkles")
            if sparkles then
                sparkles:emit_particles(16)
            end
            self:set_text("花のかけら " .. state.count .. " / " .. state.total)
        elseif name == "street_clear" then
            state.completed = true
            local seconds = state.finishedTime or state.elapsed
            self:reference("result"):set_text(
                "花のかけら " .. state.count .. " / " .. state.total ..
                string.format("    タイム %.1f 秒\n", math.floor(seconds * 10) / 10) ..
                (state.count == state.total and "街じゅうの花を見つけた！"
                    or "まだ見つけていない花のかけらがあるよ") .. "\n" ..
                best_record(state, seconds))
            if not state.finishedTime then
                self:reference("hint"):set_active(false)
                self:reference("clearSound"):play_audio()
            end
            self:reference("resultPanel"):set_active(true)
        end
    end
}
