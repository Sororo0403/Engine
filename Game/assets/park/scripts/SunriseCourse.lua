local function record_time(key, time)
    local previous = game[key]
    if type(previous) ~= "number" or previous < 0 or previous >= math.huge or time < previous then
        game[key] = time
        return string.format("%.1f 秒（更新）", time / 10)
    end
    return string.format("%.1f 秒", previous / 10)
end

return {
    start = function(_, state)
        game.sunriseUnlocked = true
        state.flowers = 0
        state.journeyTime = game.sunriseJourneyTime
        state.journeyFlowers = game.sunriseJourneyFlowers
        game.sunriseJourneyTime = nil
        game.sunriseJourneyFlowers = nil
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
    end,

    message = function(self, state, name)
        local label = self:reference("objective")
        if name == "bridge" then
            self:reference("bridgePad"):set_mesh_tint(0.30, 0.87, 0.48)
            if self:reference("gate"):is_active() then
                label:set_text("橋がつながった！ 対岸の高い足場にあるスイッチへ")
            end
        elseif name == "gate" then
            self:reference("gatePad"):set_mesh_tint(0.30, 0.87, 0.48)
            self:reference("gate"):set_active(false)
            label:set_text("見晴らし台へ  /  中央はジャンプ ［Space / A］、右は階段")
        elseif name == "flower" then
            state.flowers = state.flowers + 1
            self:reference("flowerCount"):set_text(string.format("花 %d / 2", state.flowers))
            if state.flowers == 2 then
                self:reference("finish"):set_active(true)
            end
        elseif name == "lookout_enter" and state.flowers < 2 then
            state.lookoutPrompt = label:get_text()
            label:set_text("花を探しに戻るか、このままクリア ［Enter / Y］")
        elseif name == "lookout_exit" and state.lookoutPrompt then
            self:reference("finish"):set_active(false)
            label:set_text(state.lookoutPrompt)
            state.lookoutPrompt = nil
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
            end
            self:reference("result"):set_text(result)
        end
    end
}
