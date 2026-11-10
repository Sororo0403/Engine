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
        if state.completed then
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
        local cycle = state.time
        state.time = cycle + dt

        if cycle < 1.2 then
            if cycle > 0.7 and distance < 7 and math.abs(py - y) < 1.5 then
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
                local hit = scene.cast_sphere(x, y + 0.13, z, mx / length, 0, mz / length, length, 0.79)
                local travel = hit and math.max(0, hit.distance - 0.01) or length
                self:set_local_position(x + mx * travel / length, y, z + mz * travel / length)
            end
        elseif cycle > 3.6 then
            state.time = 0
        end
    end
}
