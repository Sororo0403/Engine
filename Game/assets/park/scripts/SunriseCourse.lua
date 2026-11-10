local function record_time(key, time)
    local previous = game[key]
    if type(previous) ~= "number" or previous < 0 or previous >= math.huge or time < previous then
        game[key] = time
        return string.format("%.1f 秒（更新）", time / 10)
    end
    return string.format("%.1f 秒", previous / 10)
end

local function show_hint(self, state, text)
    if text ~= state.shownHint then
        self:reference("objective"):set_text(text)
        state.shownHint = text
    end
end

local function dock_hint(self, character)
    if not character then
        return nil
    end
    local platform = self:reference("movingPlatform")
    local _, _, platformZ = platform:get_position()
    if character.ground == platform then
        if platformZ >= 11.62 then
            return "対岸に到着  /  左の岸に下りるか、正面の段へジャンプ"
        elseif platformZ <= 6.73 then
            return "動く足場に乗船中  /  手前の岸に停泊中"
        end
        return "動く足場に乗船中  /  岸に着くまで待とう"
    end
    local x, _, z = self:reference("player"):get_position()
    local dockX, _, dockZ = self:reference("southDock"):get_position()
    if math.abs(x - dockX) <= 1.1 and z >= dockZ - 1.25 and z <= dockZ + 0.27 then
        if platformZ <= 6.73 then
            return "足場が着いた！ まっすぐ歩いて乗ろう"
        end
        return "船着き場で待とう。急ぐなら左の飛び石へ ［Space / A］"
    end
end

return {
    start = function(self, state)
        game.sunriseUnlocked = true
        state.objective = self:reference("objective"):get_text()
        state.shownHint = state.objective
        state.flowers = 0
        state.journeyTime = game.sunriseJourneyTime
        state.journeyFlowers = game.sunriseJourneyFlowers
        game.sunriseJourneyTime = nil
        game.sunriseJourneyFlowers = nil
        local volume = type(game.volumeLevel) == "number" and game.volumeLevel or 10
        volume = math.max(0, math.min(10, volume)) / 10
        for _, name in ipairs({"bridgeCue", "gateCue", "flowerCue", "clearCue"}) do
            local cue = self:reference(name)
            cue:set_audio_volume(cue:get_audio_volume() * volume)
        end
    end,

    update = function(self, state, seconds, input)
        state.elapsed = (state.elapsed or 0) + seconds
        if input.character and input.character.respawnCount > 0 then
            state.fell = true
        end
        local restart, confirm = input.actions.Restart, input.actions.Confirm
        if restart and restart.pressed then
            self:load_scene("retry")
        elseif state.lookoutPrompt and confirm and confirm.pressed then
            self:reference("finish"):set_active(true)
        end
        if not state.lookoutPrompt then
            show_hint(self, state, dock_hint(self, input.character) or state.objective)
        end
    end,

    message = function(self, state, name)
        if name == "bridge" then
            self:reference("bridgePad"):set_mesh_tint(0.30, 0.87, 0.48)
            if self:reference("gate"):is_active() then
                state.objective = "橋がつながった！ 対岸の高い足場にあるスイッチへ"
                show_hint(self, state, state.objective)
            end
        elseif name == "gate" then
            self:reference("gatePad"):set_mesh_tint(0.30, 0.87, 0.48)
            self:reference("bridgePad"):set_mesh_tint(0.30, 0.87, 0.48)
            self:reference("gate"):set_active(false)
            state.objective = "見晴らし台へ  /  中央はジャンプ ［Space / A］、右は階段"
            show_hint(self, state, state.objective)
        elseif name == "flower" then
            state.flowers = state.flowers + 1
            self:reference("flowerCount"):set_text(string.format("花 %d / 2", state.flowers))
            if state.flowers == 2 then
                self:reference("finish"):set_active(true)
            end
        elseif name == "lookout_enter" and state.flowers < 2 then
            state.lookoutPrompt = true
            show_hint(self, state, "花を探しに戻るか、このままクリア ［Enter / Y］")
        elseif name == "lookout_exit" and state.lookoutPrompt then
            self:reference("finish"):set_active(false)
            state.lookoutPrompt = nil
            show_hint(self, state, state.objective)
        elseif name == "finish" then
            state.lookoutPrompt = nil
            local time = math.floor((state.elapsed or 0) * 10)
            local best = record_time("sunriseBestClearTime", time)
            local result = string.format("水路を抜けて朝焼けの見晴らし台に到着！\nクリアタイム %.1f 秒    最速 %s",
                time / 10, best)
            if not state.fell then
                result = result .. "\n落下なし！    ベスト " .. record_time("sunriseBestCleanTime", time)
            end
            result = result .. string.format("\n花 %d / 2", state.flowers)
            if state.flowers == 2 then
                result = result .. "    水路の花をすべて集めた！"
            end
            if state.journeyTime then
                result = result .. string.format("\n3コース合計 %.1f 秒    花 %d / 13",
                    (state.journeyTime + time) / 10, state.journeyFlowers + state.flowers)
                self:reference("journeyRetry"):set_active(true)
            end
            self:reference("result"):set_text(result)
        end
    end
}
