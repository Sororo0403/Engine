return {
    message = function(self, state, name)
        if name == "stamp_collected" then
            state.collected = (state.collected or 0) + 1
            if state.collected == state.total then
                self:set_text("スタンプ 3 / 3　　時計台へ")
            else
                self:set_text(("スタンプ %d / %d　　次の店先へ"):format(state.collected, state.total))
            end
        end
    end
}
