return {
    update = function(self, state, seconds, input)
        state.elapsed = (state.elapsed or 0) + seconds
        local restart, cancel = input.actions.Restart, input.actions.Cancel
        if restart and restart.pressed then
            self:load_scene("retry")
        elseif cancel and cancel.pressed then
            self:load_scene("title")
        end
    end,

    message = function(self, state, name)
        local label = self:reference("objective")
        if name == "bridge" then
            state.bridge = true
            self:reference("bridgePad"):set_mesh_tint(0.30, 0.87, 0.48)
            label:set_text("橋がつながった！ 対岸の高い足場にあるスイッチへ")
        elseif name == "gate" then
            state.gate = true
            self:reference("gatePad"):set_mesh_tint(0.30, 0.87, 0.48)
            if not state.bridge then
                label:set_text("もう一つのスイッチは対岸。長押しジャンプで戻ろう ［Space / A］")
            end
        elseif name == "finish" then
            self:reference("result"):set_text(string.format("水路を抜けて朝焼けの見晴らし台に到着！\nクリアタイム %.1f 秒",
                state.elapsed or 0))
            return
        end

        if state.bridge and state.gate then
            self:reference("gate"):set_active(false)
            label:set_text("二つのスイッチがそろった！ 奥の段を登って見晴らし台へ")
        end
    end
}
