local function clamp(value, low, high)
    return math.min(high, math.max(low, value))
end

local function ease(value, low, high)
    local t = clamp((value - low) / (high - low), 0, 1)
    return t * t * (3 - 2 * t)
end

local function lerp(a, b, t)
    return a + (b - a) * t
end

local function clear_view(player, px, py, pz, x, y, z, clearance)
    local dx, dy, dz = x - px, y - py, z - pz
    local distance = math.sqrt(dx * dx + dy * dy + dz * dz)
    if distance < 0.01 then
        return x, y, z, distance
    end

    local available = distance
    for _, side in ipairs({-0.25, 0, 0.25}) do
        local hit = scene.raycast(px + side, py, pz, dx / distance, dy / distance,
            dz / distance, distance, nil, false, player)
        if hit then
            available = math.min(available, math.max(0.3, hit.distance - clearance))
        end
    end
    local fraction = available / distance
    return px + dx * fraction, py + dy * fraction, pz + dz * fraction, available
end

local function update(self, state, seconds)
    local player = self:reference("player")
    local px, py, pz = player:get_position()
    local reset = not state.previousX or game.streetCameraReset
    if not reset then
        local dx, dz = px - state.previousX, pz - state.previousZ
        reset = dx * dx + dz * dz > 36
    end
    state.previousX, state.previousZ = px, pz
    game.streetCameraReset = false

    local blend = reset and 1 or 1 - math.exp(-5 * seconds)
    local ground = scene.raycast(px, py, pz, 0, -1, 0, 1.8, nil, false, player)
    if ground and ground.normal_y > 0.7 then
        state.floor = lerp(state.floor or ground.y, ground.y,
            reset and 1 or 1 - math.exp(-8 * seconds))
    elseif not state.floor or reset then
        state.floor = py - 0.7
    end
    state.anchorX = reset and px or clamp(state.anchorX, px - 0.35, px + 0.35)
    state.anchorZ = reset and pz or clamp(state.anchorZ, pz - 0.45, pz + 0.45)
    local x, z = state.anchorX, state.anchorZ

    local quay = ease(z, 14, 24)
    local court = ease(z, 42, 49) * (1 - ease(z, 56, 63)) * (1 - ease(x, 4, 12))
    local roof = ease(z, 60, 70)
    local side = lerp(lerp(0.5, 2.6, quay) + 1.8 * court, 2, roof)
    local height = lerp(lerp(4.4, 4.8, quay), 5.4, roof) + state.height - 4.5
    local back = lerp(lerp(7.6, 7.8, quay), 8, roof) + state.distance - 7.6
    local aimX = x + lerp(lerp(0.2, 0.4, quay) + 1.8 * court, -0.8, roof)
    local aimY = state.floor + lerp(1.7, 1.6, math.max(quay, roof))
    local aimZ = z + lerp(3.2, 2, roof) + state.lead - 3

    local passage = self:reference("coveredPassage")
    if passage and passage:is_active() then
        local passageX, _, passageZ = passage:get_position()
        local halfWidth, _, halfLength = passage:get_scale()
        local through = (1 - ease(math.abs(px - passageX), halfWidth * 0.65, halfWidth + 0.55))
            * (1 - ease(math.abs(pz - passageZ), halfLength, halfLength + 2))
        side = lerp(side, passageX - x, through)
        height = lerp(height, 2.05, through)
        back = lerp(back, 3.6, through)
        aimX = lerp(aimX, passageX, through)
        aimY = lerp(aimY, state.floor + 1.32, through)
    end

    local tx, ty, tz = x + side, state.floor + height, z - back
    if state.avoidObstacles then
        local distance
        tx, ty, tz, distance = clear_view(player, px, py, pz, tx, ty, tz, state.clearance)
        if distance < 4.5 then
            local ax, ay, az, available = clear_view(player, px, py, pz,
                x + side * 0.65, state.floor + height + 0.6, z - back * 0.75, state.clearance)
            if available > distance + 0.4 then
                tx, ty, tz = ax, ay, az
            end
        end
    end

    local cx, cy, cz = self:get_position()
    cx, cy, cz = lerp(cx, tx, blend), lerp(cy, ty, blend), lerp(cz, tz, blend)
    if state.avoidObstacles then
        cx, cy, cz = clear_view(player, px, py, pz, cx, cy, cz, state.clearance)
    end
    local vx, vy, vz = cx - px, cy - py, cz - pz
    local distance = math.sqrt(vx * vx + vy * vy + vz * vz)
    if distance > 10.6 then
        local limit = 10.6 / distance
        cx, cy, cz = px + vx * limit, py + vy * limit, pz + vz * limit
    end
    self:set_position(cx, cy, cz)

    state.aimX = lerp(state.aimX or aimX, aimX, blend)
    state.aimY = lerp(state.aimY or aimY, aimY, blend)
    state.aimZ = lerp(state.aimZ or aimZ, aimZ, blend)
    local dx, dz = state.aimX - cx, math.max(1, state.aimZ - cz)
    local yaw = math.atan(dx, dz)
    local pitch = math.atan(cy - state.aimY, math.sqrt(dx * dx + dz * dz))
    if not reset then
        local maxTurn = 0.45 * seconds
        yaw = state.yaw + clamp((yaw - state.yaw) * blend, -maxTurn, maxTurn)
        pitch = state.pitch + clamp((pitch - state.pitch) * blend, -maxTurn, maxTurn)
    end
    state.yaw, state.pitch = yaw, pitch
    local sy, cyaw = math.sin(yaw / 2), math.cos(yaw / 2)
    local sp, cp = math.sin(pitch / 2), math.cos(pitch / 2)
    self:set_rotation(cyaw * sp, sy * cp, -sy * sp, cyaw * cp)
end

return {
    start = function(self, state)
        update(self, state, 0)
    end,
    update = update
}
