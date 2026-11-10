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
            self:reference("result"):set_text(
                "花のかけら " .. state.count .. " / " .. state.total ..
                "    タイム " .. math.floor(state.finishedTime or state.elapsed) .. " 秒\n" ..
                (state.count == state.total and "街じゅうの花を見つけた！"
                    or "まだ見つけていない花のかけらがあるよ"))
            if not state.finishedTime then
                self:reference("hint"):set_active(false)
                self:reference("clearSound"):play_audio()
            end
            self:reference("resultPanel"):set_active(true)
        end
    end
}
