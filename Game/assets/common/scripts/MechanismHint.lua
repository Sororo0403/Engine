return {
    update = function(self, state)
        local player = self:reference("player")
        local x, y, z = self:get_position()
        local px, py, pz = player:get_position()
        local dx, dy, dz = px - x, py - y, pz - z
        if dx * dx + dz * dz > state.radius * state.radius or math.abs(dy) > 2 then
            return
        end
        local open = self:reference("mechanism"):is_active() == state.openWhenActive
        local text = open and state.openText or state.closedText
        if self:reference("hint"):get_text() ~= text then
            self:reference("hint"):set_text(text)
        end
    end,
}
