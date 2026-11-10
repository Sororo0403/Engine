local function clamp(value, lower, upper)
    return math.max(lower, math.min(upper, value))
end

local function aim(visual, state, offset)
    local yaw = math.atan(state.dx, state.dz) + offset
    visual:set_rotation(0, math.sin(yaw / 2), 0, math.cos(yaw / 2))
end

local function can_see_player(self, player, x, y, z, dx, dy, dz, distance)
    if distance < 0.01 then
        return true
    end
    local hit = scene.raycast(x, y, z, dx / distance, dy / distance, dz / distance,
        distance, nil, false, self)
    return not hit or hit.object == player
end

return {
    start = function(self, state)
        local x, _, z = self:get_local_position()
        state.originX, state.originZ = x, z
        state.visualScale = {self:reference("visual"):get_scale()}
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

        local visual = self:reference("visual")
        if cycle < 1.2 then
            local detectionRadius = state.detectionRadius or 7
            local distanceSquared = dx * dx + dy * dy + dz * dz
            if cycle >= 0.7 and distanceSquared < detectionRadius * detectionRadius
                and can_see_player(self, player, x, y, z, dx, dy, dz, math.sqrt(distanceSquared)) then
                state.time = 1.2
                if state.warningEnabled then
                    self:play_audio()
                end
                local objective = self:reference("objective")
                if objective then
                    objective:send_message("street_enemy_alert")
                end
            else
                state.time = math.min(state.time, 0.75)
            end
        elseif cycle < 1.85 then
            if cycle < 1.65 and distance > 0.01 then
                state.dx, state.dz = dx / distance, dz / distance
            end
            local sx, sy, sz = table.unpack(state.visualScale)
            local crouch = clamp((cycle - 1.2) / 0.32, 0, 1)
            visual:set_scale(sx * (1 + 0.10 * crouch), sy * (1 - 0.30 * crouch),
                sz * (1 + 0.10 * crouch))
            aim(visual, state, math.sin(cycle * 45) * 0.045)
        elseif cycle < 2.45 then
            visual:set_scale(table.unpack(state.visualScale))
            aim(visual, state, 0)
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
