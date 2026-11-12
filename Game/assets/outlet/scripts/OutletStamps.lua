return {
    message = function(self, state, name, data)
        if name == "rush_retry" then
            self:set_text("もう一度、通りのラッシュへ。［J / X］で攻撃")
        elseif name == "rush_cleared" and data and data.boss then
            self:set_text("時計台の主を倒した！　鐘の前へ")
        elseif name == "stamp_collected" then
            state.collected = (state.collected or 0) + 1
            if state.collected == state.total then
                self:set_text("スタンプ 3 / 3　　時計台へ")
            else
                self:set_text(("スタンプ %d / %d　　次の店先へ"):format(state.collected, state.total))
            end
        end
    end
}
