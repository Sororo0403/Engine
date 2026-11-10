return {
    update = function(self, state, seconds)
        if state.finishTime then
            state.finishTime = state.finishTime - seconds
            if state.finishTime <= 0 then
                state.finishTime = nil
                self:reference("resultCounter"):send_message("street_clear")
            end
            return
        end
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
        local counter = self:reference("resultCounter")
        counter:send_message("street_goal_reached")

        local camera = self:reference("finishCamera")
        if camera then
            scene.set_camera(camera)
        end
        player:send_message("street_clear")
        state.finishTime = state.finishDelay or 0
    end
}
