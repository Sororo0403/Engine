local function record_time(key, time)
    local previous = game[key]
    if type(previous) ~= "number" or previous < 0 or previous >= math.huge then
        previous = nil
    end
    local improved = not previous or time < previous
    if improved then
        game[key] = time
    end
    return string.format("%.1f 秒%s", (improved and time or previous) / 10,
        improved and "（更新）" or "")
end

return {
    start = function(self, state)
        state.pondHint = self:reference("label"):get_text()
        state.transferHint = false
        state.elapsed = 0
        state.ferryDocked = true
        local volume = type(game.volumeLevel) == "number" and game.volumeLevel or 10
        volume = math.max(0, math.min(10, volume)) / 10
        for _, name in ipairs({"ferry", "switchCue", "gateCue", "flowerCue", "clearCue"}) do
            local cue = self:reference(name)
            cue:set_audio_volume(cue:get_audio_volume() * volume)
        end
    end,

    update = function(self, state, seconds, input)
        if state.finished then
            return
        end
        state.elapsed = state.elapsed + seconds
        if state.farBank or not input.character then
            return
        end

        local ferry = self:reference("ferry")
        local _, _, z = ferry:get_position()
        local docked = z <= state.nearDockZ + 0.08 or z >= state.farDockZ - 0.08
        if docked and not state.ferryDocked then
            ferry:play_audio()
        end
        state.ferryDocked = docked

        local showTransfer = input.character.ground == ferry
            and not self:reference("ferryFlower"):is_active()
            and self:reference("stoneFlower"):is_active()
        if showTransfer ~= state.transferHint then
            state.transferHint = showTransfer
            self:reference("label"):set_text(showTransfer
                and "中央の飛び石と並んだら左へジャンプ ［Space / A 長押し］" or state.pondHint)
        end
    end,

    message = function(self, state, name)
        if name == "park_flower" then
            state.flowers = (state.flowers or 0) + 1
            self:reference("flowerCue"):play_audio()
            self:reference("flowerLabel"):set_text(
                "公園の花 " .. state.flowers .. " / " .. state.totalFlowers)
            return
        end

        if name == "park_cleared" then
            state.finished = true
            game.parkUnlocked = true
            self:reference("clearCue"):play_audio()
            local flowers = state.flowers or 0
            local result = self:reference("result")
            local summary = "公園の花 " .. flowers .. " / " .. state.totalFlowers
            if flowers == state.totalFlowers then
                summary = summary .. "    池と庭の花をすべて見つけた！"
            end
            local time = math.floor(state.elapsed * 10)
            local clear = record_time("parkBestClearTime", time)
            local times = string.format("タイム %.1f 秒    最速クリア %s", time / 10, clear)
            if flowers == state.totalFlowers then
                times = times .. "    全収集ベスト " .. record_time("parkBestFullTime", time)
            end
            result:set_text(result:get_text() .. "\n" .. summary .. "\n" .. times)
            return
        end

        local label = self:reference("label")
        if name == "far_bank" then
            state.farBank = true
            if not state.west and not state.east then
                label:set_text("スイッチの庭  /  左の段と右の生け垣にあるスイッチを踏もう  0 / 2")
            end
            return
        end

        if name ~= "west" and name ~= "east" then
            return
        end
        if state[name] then
            return
        end

        state[name] = true
        self:reference(name):set_mesh_tint(0.30, 0.87, 0.48)
        local count = (state.west and 1 or 0) + (state.east and 1 or 0)
        if count == 2 then
            self:reference("gate"):set_active(false)
            self:reference("gateCue"):play_audio()
            self:reference("goal"):set_active(true)
            label:set_text("門が開いた！  /  飛び石を跳ぶか、右の段を登って展望台へ")
        else
            self:reference("switchCue"):play_audio()
            label:set_text("スイッチ 1 / 2  /  もう片方を踏んで門を開けよう")
        end
    end
}
