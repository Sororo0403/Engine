local function barriers(self, enabled)
    self:reference("barriers"):set_active(enabled)
end

local function hide_waves(self, state)
    for i = 1, state.waveCount do
        self:reference("wave" .. i):set_active(false)
    end
end

local function can_spawn(self, state, x, y, z, placed)
    local px, py, pz = self:reference("player"):get_position()
    if math.abs(y - py) < 2 and (x - px) ^ 2 + (z - pz) ^ 2 < 1.5 ^ 2 then
        return false
    end
    for _, point in ipairs(placed) do
        if (x - point[1]) ^ 2 + (z - point[3]) ^ 2 < 1.35 ^ 2 then
            return false
        end
    end
    if state.requireFooting then
        local floor = scene.raycast(x, y + 0.12, z, 0, -1, 0, y + 0.24)
        return floor and floor.normal_y > 0.5
    end
    return true
end

local function begin_wave(self, state)
    local wave = self:reference("wave" .. state.wave)
    local enemies, placed = wave:get_children(), {}
    for i, enemy in ipairs(enemies) do
        local spawn = state.spawns[enemy:get_id()]
        local x, y, z = table.unpack(spawn)
        if not can_spawn(self, state, x, y, z, placed) then
            local available = false
            for _, point in ipairs(state.fallbackSpawns or {}) do
                x, z = point[1], point[2]
                if can_spawn(self, state, x, y, z, placed) then
                    available = true
                    break
                end
            end
            if not available then
                return false
            end
        end
        placed[i] = {x, y, z}
    end

    for i, enemy in ipairs(enemies) do
        enemy:set_position(table.unpack(placed[i]))
        enemy:set_active(true)
    end
    wave:set_active(true)
    for _, enemy in ipairs(enemies) do
        enemy:send_message("rush_reset")
    end
    state.remaining = nil
    return true
end

return {
    start = function(self, state)
        state.spawns = {}
        for i = 1, state.waveCount do
            for _, enemy in ipairs(self:reference("wave" .. i):get_children()) do
                state.spawns[enemy:get_id()] = {enemy:get_position()}
            end
        end
    end,

    message = function(self, state, name)
        if name ~= "begin_rush" or state.running or state.completed then
            return
        end

        state.running, state.wave, state.delay = true, 1, state.waveDelay
        barriers(self, true)
        self:reference("hint"):set_text((state.title or "店先のラッシュ") .. "　［J / X］で攻撃")
    end,

    update = function(self, state, seconds, input)
        if input.character and input.character.respawnCount ~= (state.respawns or 0) then
            state.respawns = input.character.respawnCount
            if state.running then
                hide_waves(self, state)
                barriers(self, false)
                state.running = false
                local bossLife = self:reference("bossLife")
                if bossLife then
                    bossLife:set_active(false)
                end
                self:reference("hint"):send_message("rush_retry")
            end
        end
        if not state.running then
            return
        end

        if state.delay then
            state.delay = math.max(0, state.delay - seconds)
            if state.delay > 0 then
                return
            end
            if not begin_wave(self, state) then
                state.delay = 0.2
                return
            end
            state.delay = nil
        end

        local remaining = 0
        for _, enemy in ipairs(self:reference("wave" .. state.wave):get_children()) do
            if enemy:is_active() then
                remaining = remaining + 1
            end
        end
        if remaining ~= state.remaining then
            state.remaining = remaining
            if not state.bossEncounter then
                self:reference("hint"):set_text(("%s %d / %d　　残り %d"):format(
                    state.title or "ラッシュ", state.wave, state.waveCount, remaining))
            end
        end
        if remaining > 0 then
            return
        end

        self:reference("wave" .. state.wave):set_active(false)
        if state.wave == state.waveCount then
            state.running, state.completed = false, true
            barriers(self, false)
            self:reference("player"):send_message("rush_cleared")
            self:reference("hint"):send_message("rush_cleared", {boss = state.bossEncounter or false})
            self:reference("clearSound"):play_audio()
            local goal = self:reference("goal")
            if goal then
                goal:set_active(true)
            end
        else
            if state.healBetweenWaves then
                self:reference("player"):send_message("rush_wave_heal")
            end
            state.wave = state.wave + 1
            state.delay = state.waveDelay
        end
    end
}
