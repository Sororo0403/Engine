return {
    update = function(self, state)
        if state.completed or game.streetDeathActive then
            return
        end

        local player = self:reference("player")
        local x, y, z = self:get_position()
        local px, py, pz = player:get_position()
        local dx, dz = px - x, pz - z
        if dx * dx + dz * dz >= state.radius * state.radius or math.abs(py - y) >= state.height then
            return
        end

        state.completed = true
        player:set_character_input_enabled(false)
        self:reference("resultCounter"):send_message("street_clear")
        player:send_message("street_clear")
    end
}
