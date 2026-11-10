return {
    update = function(self, state, seconds)
        if not state.completed and not game.streetDeathActive then
            state.elapsed = state.elapsed + seconds
        end
    end,

    message = function(self, state, name)
        if state.completed then
            return
        end

        if name == "collect_flower" and state.count < state.total then
            state.count = state.count + 1
            self:set_text("花のかけら " .. state.count .. " / " .. state.total)
        elseif name == "street_clear" then
            state.completed = true
            self:reference("result"):set_text(
                "花のかけら " .. state.count .. " / " .. state.total ..
                "    タイム " .. math.floor(state.elapsed) .. " 秒")
            self:reference("hint"):set_active(false)
            self:reference("resultPanel"):set_active(true)
            self:reference("clearSound"):play_audio()
        end
    end
}
