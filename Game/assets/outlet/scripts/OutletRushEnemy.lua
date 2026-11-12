local function reset(self, state)
    state.life = state.maximumLife
    state.phase, state.elapsed = "approach", -(state.startDelay or 0)
    state.hitFlash, state.fallSpeed, state.attackIndex = 0, 0, 0
    state.struck = false
    self:reference("visual"):set_scale(1, 1, 1)
    self:reference("visual"):set_mesh_tint(table.unpack(state.tint))
    local weapon, warning, life = self:reference("weapon"), self:reference("warning"), self:reference("bossLife")
    if weapon then
        weapon:set_rotation(0, 0, 0, 1)
    end
    if warning then
        warning:set_active(false)
    end
    if life then
        life:set_active(true)
        life:set_text(("時計台の主　%d / %d"):format(state.life, state.maximumLife))
    end
end

local function move(self, player, x, y, z, dx, dz)
    local distance = math.sqrt(dx * dx + dz * dz)
    if distance < 0.0001 then
        return
    end
    local hit, fraction = scene.cast_collider(self, dx, 0, dz)
    if hit and hit ~= player then
        fraction = math.max(0, fraction - 0.015 / distance)
    end
    self:set_position(x + dx * fraction, y, z + dz * fraction)
end

local function footing(self, state, x, y, z, dt)
    if not state.unstable then
        return true
    end
    for _, offset in ipairs({{0, 0}, {-0.28, 0}, {0.28, 0}, {0, -0.28}, {0, 0.28}}) do
        local hit = scene.raycast(x + offset[1], y + 0.1, z + offset[2], 0, -1, 0,
            state.footHeight + 0.22, nil, false, self)
        if hit and hit.normal_y > 0.5 and y >= hit.y + state.footHeight - 0.08 then
            state.fallSpeed = 0
            return true
        end
    end
    state.fallSpeed = math.min(20, state.fallSpeed + 20 * dt)
    self:set_position(x, y - state.fallSpeed * dt, z)
    if y < -2.5 then
        self:set_active(false)
    end
    return false
end

local function recover(self, state)
    state.phase, state.elapsed = "recover", 0
    local weapon = self:reference("weapon")
    if weapon then
        weapon:set_rotation(0, 0, 0, 1)
    end
end

return {
    start = function(self, state)
        state.tint = {self:reference("visual"):get_mesh_tint()}
        reset(self, state)
    end,

    message = function(self, state, name)
        if name == "rush_reset" then
            reset(self, state)
        elseif name == "outlet_hit" and state.life > 0 then
            state.life = state.life - 1
            local life = self:reference("bossLife")
            if life then
                life:set_text(("時計台の主　%d / %d"):format(state.life, state.maximumLife))
            end
            if state.life == 0 then
                self:set_active(false)
                if life then
                    life:set_active(false)
                end
            else
                state.hitFlash = 0.24
                if not state.boss then
                    recover(self, state)
                    local warning = self:reference("warning")
                    if warning then
                        warning:set_active(false)
                    end
                end
            end
        end
    end,

    update = function(self, state, seconds, input)
        local dt = math.min(seconds, 0.1)
        state.elapsed = state.elapsed + dt
        if state.elapsed < 0 then
            return
        end
        local player = self:reference("player")
        local x, y, z = self:get_position()
        if not footing(self, state, x, y, z, dt) then
            return
        end
        local px, py, pz = player:get_position()
        local dx, dz = px - x, pz - z
        local distance = math.sqrt(dx * dx + dz * dz)
        local visual = self:reference("visual")
        local weapon, warning = self:reference("weapon"), self:reference("warning")
        state.hitFlash = math.max(0, state.hitFlash - dt)
        local tint = state.hitFlash > 0 and 0.45 or 1
        local r, g, b = table.unpack(state.tint)
        visual:set_mesh_tint(r * tint, g * tint, b * tint)
        if state.phase == "approach" then
            if distance > 0.01 then
                state.dx, state.dz = dx / distance, dz / distance
            end
            if distance <= state.warningDistance then
                state.phase, state.elapsed, state.struck = "warning", 0, false
                state.attackIndex = state.attackIndex + 1
                state.nextAttack = state.bag and "swing" or "charge"
                if state.boss then
                    state.nextAttack = ({"swing", "charge", "spin"})[(state.attackIndex - 1) % 3 + 1]
                end
                if warning then
                    warning:set_active(true)
                end
                self:play_audio()
            else
                move(self, player, x, y, z, state.dx * state.moveSpeed * dt,
                    state.dz * state.moveSpeed * dt)
            end
        elseif state.phase == "warning" then
            local crouch = math.min(1, state.elapsed / state.warningSeconds)
            visual:set_scale(1 + crouch * 0.12, 1 - crouch * 0.26, 1 + crouch * 0.12)
            if weapon then
                local angle = -3.2 * crouch
                weapon:set_rotation(0, math.sin(angle / 2), 0, math.cos(angle / 2))
            end
            if state.elapsed >= state.warningSeconds then
                state.phase, state.elapsed = state.nextAttack, 0
                visual:set_scale(1, 1, 1)
                if warning then
                    warning:set_active(false)
                end
            end
        elseif state.phase == "charge" then
            move(self, player, x, y, z, state.dx * state.chargeSpeed * dt,
                state.dz * state.chargeSpeed * dt)
            if state.elapsed >= state.chargeSeconds then
                recover(self, state)
            end
        elseif state.phase == "swing" or state.phase == "spin" then
            local progress = math.min(1, state.elapsed / state.swingSeconds)
            local angle = state.phase == "spin" and progress * math.pi * 2
                or -3.2 + progress * 3.4
            weapon:set_rotation(0, math.sin(angle / 2), 0, math.cos(angle / 2))
            local jumping = input.character and not input.character.grounded and py - y > 0.95
            local facing = distance < 0.01 or (dx * state.dx + dz * state.dz) / distance > -0.15
            if not state.struck and not jumping and distance < state.attackRadius
                and (state.phase == "spin" or facing) and math.abs(py - y) < 2.3 then
                player:send_message("enemy_contact")
                state.struck = true
            end
            if progress == 1 then
                recover(self, state)
            end
        elseif state.elapsed >= state.recoverySeconds then
            state.phase, state.elapsed = "approach", 0
        end
        if state.dx then
            local yaw = math.atan(state.dx, state.dz)
            visual:set_rotation(0, math.sin(yaw / 2), 0, math.cos(yaw / 2))
        end
    end
}
