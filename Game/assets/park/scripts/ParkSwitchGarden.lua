return {
    message = function(self, state, name)
        if name == "far_bank" then
            if not state.west and not state.east then
                self:set_text("スイッチの庭  /  左の段と右の生け垣にあるスイッチを踏もう  0 / 2")
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
            self:reference("goal"):set_active(true)
            self:set_text("スイッチ 2 / 2  /  門が開いた。展望台へ進もう")
        else
            self:set_text("スイッチ 1 / 2  /  もう片方を踏んで門を開けよう")
        end
    end
}
