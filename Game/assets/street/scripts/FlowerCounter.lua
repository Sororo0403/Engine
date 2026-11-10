return {
    update = function(self, state, seconds)
        if not state.completed then
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
        end
    end
}
