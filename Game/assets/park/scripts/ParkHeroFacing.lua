return {
    update = function(self, state, seconds, input)
        local x, z = input.moveX, input.moveZ
        if seconds <= 0 or x * x + z * z < 0.01 or not self:is_character_input_enabled() then
            return
        end

        local yaw = state.yaw or 0
        local turn = (math.atan(x, z) - yaw + math.pi) % (2 * math.pi) - math.pi
        if math.abs(turn) < 0.001 then
            return
        end

        yaw = yaw + turn * (1 - math.exp(-16 * seconds))
        state.yaw = (yaw + math.pi) % (2 * math.pi) - math.pi
        self:reference("visual"):set_rotation(0, math.sin(yaw / 2), 0, math.cos(yaw / 2))
    end
}
