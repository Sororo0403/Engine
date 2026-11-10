return {
    message = function(self, state, name)
        if name == "park_flower" then
            state.flowers = (state.flowers or 0) + 1
            self:reference("flowerLabel"):set_text(
                "公園の花 " .. state.flowers .. " / " .. state.totalFlowers)
            return
        end

        if name == "park_cleared" then
            game.parkUnlocked = true
            local flowers = state.flowers or 0
            local result = self:reference("result")
            local summary = "公園の花 " .. flowers .. " / " .. state.totalFlowers
            if flowers == state.totalFlowers then
                summary = summary .. "    池と庭の花をすべて見つけた！"
            end
            result:set_text(result:get_text() .. "\n" .. summary)
            return
        end

        local label = self:reference("label")
        if name == "far_bank" then
            if not state.west and not state.east then
                label:set_text("スイッチの庭  /  左の段と右の生け垣にあるスイッチを踏もう  0 / 2")
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
            label:set_text("門が開いた！  /  飛び石を跳ぶか、右の段を登って展望台へ")
        else
            label:set_text("スイッチ 1 / 2  /  もう片方を踏んで門を開けよう")
        end
    end
}
