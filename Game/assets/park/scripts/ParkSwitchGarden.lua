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

local function open_flower_bridge(self, state)
    if state.west and state.east and state.flowers == state.totalFlowers then
        self:reference("flowerBridge"):set_active(true)
        self:reference("label"):set_text("花がそろった！  /  左の花の橋を渡って展望台へ")
    end
end

local flowerLocations = {
    {"ferry_flower", "いかだ"},
    {"stone_flower", "飛び石"},
    {"garden_flower", "庭"}
}

local function update_flower_label(self, state)
    local remaining = {}
    for _, flower in ipairs(flowerLocations) do
        if not state[flower[1]] then
            remaining[#remaining + 1] = flower[2]
        end
    end
    local suffix = #remaining > 0 and table.concat(remaining, "・") or "全収集"
    self:reference("flowerLabel"):set_text(
        "花 " .. state.flowers .. "/" .. state.totalFlowers .. "　" .. suffix)
end

return {
    start = function(self, state)
        state.journeyStreetTime = game.journeyStreetTime
        game.journeyStreetTime = nil
        state.pondHint = self:reference("label"):get_text()
        state.transferHint = false
        state.elapsed = 0
        state.ferryDocked = true
        state.flowers = 0
        update_flower_label(self, state)
        local volume = type(game.volumeLevel) == "number" and game.volumeLevel or 10
        volume = math.max(0, math.min(10, volume)) / 10
        for _, name in ipairs({"ferry", "switchCue", "gateCue", "flowerCue", "clearCue", "cartCue"}) do
            local cue = self:reference(name)
            cue:set_audio_volume(cue:get_audio_volume() * volume)
        end
    end,

    update = function(self, state, seconds, input)
        if state.finished then
            return
        end
        state.elapsed = state.elapsed + seconds
        if input.character and input.character.respawnCount > 0 then
            state.missedCleanRun = true
        end
        if state.cartRecovery then
            state.cartRecovery = state.cartRecovery - seconds
            if state.cartRecovery <= 0 then
                self:reference("player"):set_character_input_enabled(true)
                self:reference("label"):set_text(state.cartHint)
                state.cartRecovery = nil
                state.cartHint = nil
            end
            return
        end
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
        if name == "cart_hit" then
            local label = self:reference("label")
            state.missedCleanRun = true
            state.cartRecovery = 0.6
            state.cartHint = label:get_text()
            label:set_text("カートにぶつかった！ 対岸から庭へ戻ろう")
            self:reference("player"):set_character_input_enabled(false)
            return
        end

        if name == "ferry_flower" or name == "stone_flower" or name == "garden_flower" then
            if state[name] then
                return
            end
            state[name] = true
            state.flowers = state.flowers + 1
            self:reference("flowerCue"):play_audio()
            update_flower_label(self, state)
            open_flower_bridge(self, state)
            return
        end

        if name == "park_cleared" then
            state.finished = true
            game.parkUnlocked = true
            self:reference("clearCue"):play_audio()
            local flowers = state.flowers
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
            local clean = "次の挑戦：落下もカートとの接触もなしでクリア"
            if not state.missedCleanRun then
                clean = "ノーミスクリア！    ベスト " .. record_time("parkBestCleanTime", time)
            end
            local journey = ""
            if type(state.journeyStreetTime) == "number" then
                local total = state.journeyStreetTime + time
                journey = string.format("\n街＋公園 %.1f 秒    通しベスト %s", total / 10,
                    record_time("journeyBestTime", total))
            end
            result:set_text(result:get_text() .. "\n" .. summary .. "\n" .. times .. "\n" .. clean .. journey)
            return
        end

        local label = self:reference("label")
        if name == "far_bank" then
            state.farBank = true
            if not state.west and not state.east then
                label:set_text("スイッチの庭  /  カートをよけ、左右のスイッチを踏もう  0 / 2")
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
            label:set_text("門が開いた！  /  飛び石か右の段へ。花3つで左の橋が開く")
            open_flower_bridge(self, state)
        else
            self:reference("switchCue"):play_audio()
            label:set_text("スイッチ 1 / 2  /  カートに気をつけて中央を抜けるか、外側を回ろう")
        end
    end
}
