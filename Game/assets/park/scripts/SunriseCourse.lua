local function record_time(key, time)
    local previous = game[key]
    if type(previous) ~= "number" or previous < 0 or previous >= math.huge or time < previous then
        game[key] = time
        return string.format("%.1f 秒（更新）", time / 10)
    end
    return string.format("%.1f 秒", previous / 10)
end

return {
    start = function()
        game.sunriseUnlocked = true
    end,

    update = function(self, state, seconds, input)
        state.elapsed = (state.elapsed or 0) + seconds
        if input.character and input.character.respawnCount > 0 then
            state.fell = true
        end
        local restart = input.actions.Restart
        if restart and restart.pressed then
            self:load_scene("retry")
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
        elseif name == "finish" then
            local time = math.floor((state.elapsed or 0) * 10)
            local best = record_time("sunriseBestClearTime", time)
            local result = string.format("水路を抜けて朝焼けの見晴らし台に到着！\nクリアタイム %.1f 秒    最速 %s",
                time / 10, best)
            if not state.fell then
                result = result .. "\n落下なし！    ベスト " .. record_time("sunriseBestCleanTime", time)
            end
            self:reference("result"):set_text(result)
        end
    end
}
