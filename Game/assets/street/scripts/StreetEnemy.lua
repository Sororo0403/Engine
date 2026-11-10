local function clamp(value, lower, upper)
    return math.max(lower, math.min(upper, value))
end

return {
    start = function(self, state)
        local x, _, z = self:get_local_position()
        state.originX, state.originZ = x, z
    end,

    message = function(_, state, name)
        if name == "street_clear" then
            state.completed = true
        end
    end,

    update = function(self, state, seconds)
        if state.completed or game.streetDeathActive then
            return
        end

        local player = self:reference("player")
        if not player or not player:is_active() then
            return
        end

        local dt = math.min(seconds, 0.1)
        local x, y, z = self:get_local_position()
        local px, py, pz = player:get_position()
        local dx, dz = px - x, pz - z
        local distance = math.sqrt(dx * dx + dz * dz)
        local dy = py - y
        local cycle = state.time
        state.time = cycle + dt

        if cycle < 1.2 then
            if cycle > 0.7 and dx * dx + dy * dy + dz * dz < 49 then
                state.time = 1.2
            end
        elseif cycle < 1.85 then
            if distance > 0.01 then
                state.dx, state.dz = dx / distance, dz / distance
            end
            local yaw = math.atan(state.dx, state.dz) + math.sin(cycle * 45) * 0.045
            self:reference("visual"):set_rotation(0, math.sin(yaw / 2), 0, math.cos(yaw / 2))
        elseif cycle < 2.45 then
            local yaw = math.atan(state.dx, state.dz)
            self:reference("visual"):set_rotation(0, math.sin(yaw / 2), 0, math.cos(yaw / 2))
            local limit, speed = state.range, state.speed
            local nx = clamp(x + state.dx * dt * speed, state.originX - limit, state.originX + limit)
            local nz = clamp(z + state.dz * dt * speed, state.originZ - limit, state.originZ + limit)
            local mx, mz = nx - x, nz - z
            local length = math.sqrt(mx * mx + mz * mz)
            if length > 0.0001 then
                local hit, fraction = scene.cast_collider(self, mx, 0, mz)
                if hit and hit ~= player then
                    fraction = math.max(0, fraction - 0.01 / length)
                end
                self:set_local_position(x + mx * fraction, y, z + mz * fraction)
            end
        elseif cycle > 3.6 then
            state.time = 0
        end
    end
}
