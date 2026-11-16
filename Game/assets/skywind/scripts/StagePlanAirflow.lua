return {
    update = function(self, state, seconds, input)
        if not input.character then
            return
        end

        local player = self:reference("player")
        local px, py, pz = player:get_position()
        local vx, vy, vz = 0, 0, 0
        for _, field in ipairs(state.fields) do
            local volume = self:reference(field.reference)
            if volume:is_active() and (not field.airborneOnly or not input.character.grounded) then
                local x, y, z = volume:world_to_local(px, py, pz)
                if math.abs(x) <= 1 and math.abs(y) <= 1 and math.abs(z) <= 1 then
                    vx = vx + field.velocity[1]
                    vy = vy + field.velocity[2]
                    vz = vz + field.velocity[3]
                end
            end
        end
        player:set_character_airflow(vx, vy, vz)
    end
}
