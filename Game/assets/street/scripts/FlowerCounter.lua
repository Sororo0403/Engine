return {
    message = function(self, state, name)
        if name == "collect_flower" and state.count < state.total then
            state.count = state.count + 1
            self:set_text("花のかけら " .. state.count .. " / " .. state.total)
        end
    end
}
