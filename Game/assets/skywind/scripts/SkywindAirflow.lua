return {
    update = function(self, state, seconds, input)
        if not input.character then
            return
        end

        state.time = (state.time or 0) + seconds
        local player = self:reference("player")
        local px, py, pz = player:get_position()
        local wx, wy, wz = 0, 0, 0
        local activeField
        for _, field in ipairs(state.fields) do
            local zone = self:reference(field.reference)
            local x, y, z = zone:world_to_local(px, py, pz)
            if zone:is_active() and math.abs(x) <= 1 and math.abs(y) <= 1 and math.abs(z) <= 1
                and (not field.airborneOnly or not input.character.grounded) then
                activeField = field
                local pulse = 1
                if field.period then
                    pulse = 0.55 + 0.45 * math.sin(state.time * math.pi * 2 / field.period + (field.phase or 0))
                end
                wx = wx + field.velocity[1] * pulse
                wy = wy + field.velocity[2] * pulse
                wz = wz + field.velocity[3] * pulse
            end
        end

        player:set_character_airflow(wx, wy, wz)
        local current = activeField and activeField.reference or ""
        if state.lastField ~= current then
            state.lastField = current
            self:reference("hint"):send_message(activeField and
                (activeField.velocity[2] > 0 and "updraft" or "crosswind") or "relay")
        end
    end
}
