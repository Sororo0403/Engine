local function show_life(self, state)
    self:reference("life"):set_text(("ライフ %d / %d"):format(state.life, state.maximumLife))
end

local function stop_attack(self, state)
    state.attack = 0
    state.hit = {}
    self:reference("attackFlash"):set_active(false)
    self:reference("visual"):set_scale(1, 1, 1)
end

local function damage(self, state)
    if state.invulnerable > 0 or state.life <= 0 then
        return
    end

    state.life = state.life - 1
    state.invulnerable = state.invulnerabilitySeconds
    self:reference("damageSound"):play_audio()
    show_life(self, state)
    if state.life == 0 then
        stop_attack(self, state)
        local x, y, z = self:get_position()
        self:reference("respawnRequest"):set_position(x, y, z)
        self:reference("respawnRequest"):set_active(true)
    end
end

local function on_contact(self, state, other)
    if other then
        local tag = other:get_tag()
        if tag == "outlet_rush_enemy" or tag == "outlet_customer" then
            damage(self, state)
        end
    end
end

return {
    collision_enter = on_contact,
    collision_stay = on_contact,

    start = function(self, state)
        state.life = state.maximumLife
        state.cooldown, state.attack, state.invulnerable = 0, 0, 0
        state.hit = {}
        show_life(self, state)
    end,

    message = function(self, state, name)
        if name == "enemy_contact" then
            damage(self, state)
        elseif name == "rush_wave_heal" and state.life > 0 then
            state.life = math.min(state.maximumLife, state.life + 1)
            show_life(self, state)
        elseif name == "rush_cleared" then
            state.life = state.maximumLife
            show_life(self, state)
        end
    end,

    update = function(self, state, seconds, input)
        local character = input.character
        if character and character.respawnCount ~= (state.respawns or 0) then
            state.respawns = character.respawnCount
            state.life = state.maximumLife
            state.invulnerable = state.invulnerabilitySeconds
            state.cooldown = 0
            self:reference("respawnRequest"):set_active(false)
            stop_attack(self, state)
            show_life(self, state)
        end

        local dt = math.min(seconds, 0.1)
        state.cooldown = math.max(0, state.cooldown - dt)
        state.invulnerable = math.max(0, state.invulnerable - dt)
        local visual = self:reference("visual")
        local dim = state.invulnerable > 0 and math.floor(state.invulnerable / 0.12) % 2 == 0
        visual:set_mesh_tint(dim and 0.45 or 1, dim and 0.45 or 1, dim and 0.45 or 1)
        if state.life <= 0 or not self:is_character_input_enabled() then
            return
        end

        local moving = input.moveX * input.moveX + input.moveZ * input.moveZ > 0.01
        if moving then
            local yaw = state.yaw or 0
            local turn = (math.atan(input.moveX, input.moveZ) - yaw + math.pi) % (math.pi * 2) - math.pi
            state.yaw = yaw + turn * (1 - math.exp(-16 * dt))
            visual:set_rotation(0, math.sin(state.yaw / 2), 0, math.cos(state.yaw / 2))
        end

        local attack = input.actions.Attack
        if attack and attack.pressed and state.cooldown == 0 then
            state.attack = state.attackSeconds
            state.cooldown = state.cooldownSeconds
            state.hit = {}
            self:reference("attackFlash"):set_active(true)
            self:reference("attackSound"):play_audio()
        end

        if state.attack <= 0 then
            return
        end

        state.attack = math.max(0, state.attack - dt)
        local progress = 1 - state.attack / state.attackSeconds
        local radius = state.attackDistance * (0.48 + 0.52 * math.min(1, progress / 0.65))
        local x, y, z = self:get_position()
        for _, tag in ipairs({"outlet_rush_enemy", "outlet_customer"}) do
            for _, enemy in ipairs(scene.find_all_by_tag(tag)) do
                local id = enemy:get_id()
                local ex, ey, ez = enemy:get_position()
                local dx, dy, dz = ex - x, ey - y, ez - z
                local distance = math.sqrt(dx * dx + dy * dy + dz * dz)
                if not state.hit[id] and distance < radius then
                    local hit
                    if distance > 0.001 then
                        hit = scene.raycast(x, y, z, dx / distance, dy / distance,
                            dz / distance, distance, nil, false, self)
                    end
                    if not hit or hit.object == enemy then
                        state.hit[id] = true
                        if tag == "outlet_rush_enemy" then
                            enemy:send_message("outlet_hit")
                        else
                            enemy:set_active(false)
                        end
                        local sparks = self:reference("hitSparks")
                        sparks:set_position(ex, ey, ez)
                        sparks:emit_particles(10)
                        self:reference("hitSound"):play_audio()
                    end
                end
            end
        end

        self:reference("attackFlash"):set_scale(radius, 1, radius)
        visual:set_scale(1, 1 - 0.16 * math.sin(math.pi * progress), 1)
        if state.attack == 0 then
            stop_attack(self, state)
        end
    end
}
