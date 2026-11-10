local function show_life(self, state)
    self:reference("life"):set_text("ライフ " .. state.life .. " / " .. state.maximumLife)
end

local function smooth(amount)
    local t = math.min(1, math.max(0, amount))
    return t * t * (3 - 2 * t)
end

local function pose(self, tilt, height)
    local visual = self:reference("visual")
    visual:set_rotation(0, 0, math.sin(tilt / 2), math.cos(tilt / 2))
    visual:set_scale(1, height, 1)
end

local function update_death(self, state, seconds)
    local blackout = state.deathHold + state.deathClose
    local opening = blackout + state.deathCovered
    local finish = opening + state.deathReveal
    local t = math.min(finish, state.deathTime + seconds)
    local shade = self:reference("deathShade")

    if t < blackout then
        local reaction = smooth((t - state.deathReactionDelay) / state.deathReactionSeconds)
        local bounce = math.sin(reaction * math.pi)
        pose(self, -0.48 * reaction, 1 - 0.34 * bounce)
        shade:set_image_color(0, 0, 0, smooth((t - state.deathHold) / state.deathClose))
    elseif not state.deathReturned then
        -- 復帰のフレームは暗幕を閉じた状態で表示する。
        shade:set_image_color(0, 0, 0, 1)
        self:set_position(state.spawnX, state.spawnY, state.spawnZ)
        state.life = state.maximumLife
        state.deathReturned = true
        t = blackout
        pose(self, 0, 1)
        show_life(self, state)
    else
        local reveal = smooth((t - opening) / state.deathReveal)
        shade:set_image_color(0, 0, 0, 1 - reveal)
        pose(self, 0, 1 + 0.12 * math.sin(reveal * math.pi))
    end

    state.deathTime = t
    if t >= finish then
        pose(self, 0, 1)
        self:set_character_speeds(state.moveSpeed, state.jumpSpeed)
        state.invulnerable = state.invulnerabilitySeconds
        state.deathTime = nil
        state.deathReturned = nil
    end
end

local function on_contact(self, state, other)
    if not other or other:get_tag() ~= "street_enemy" or state.invulnerable > 0 or state.deathTime then
        return
    end

    state.life = state.life - 1
    state.invulnerable = state.invulnerabilitySeconds
    state.reaction = state.reactionSeconds
    if state.life == 0 then
        state.deathTime = 0
        state.reaction = 0
        state.moveSpeed, state.jumpSpeed = self:get_character_speeds()
        self:set_character_speeds(0.001, 0.001)
    end
    show_life(self, state)
end

return {
    start = function(self, state)
        state.spawnX, state.spawnY, state.spawnZ = self:get_position()
    end,

    message = function(self, state, name)
        if name == "courtyard" or name == "roof" then
            state.spawnX, state.spawnY, state.spawnZ = self:reference(name):get_position()
        end
    end,

    collision_enter = on_contact,
    collision_stay = on_contact,

    update = function(self, state, seconds, input)
        if state.deathTime then
            update_death(self, state, seconds)
            return
        end

        state.invulnerable = math.max(0, state.invulnerable - seconds)

        if input.character and state.respawnCount ~= input.character.respawnCount then
            state.respawnCount = input.character.respawnCount
            state.life = state.maximumLife
            state.invulnerable = state.invulnerabilitySeconds
            state.reaction = 0
            self:reference("visual"):set_rotation(0, 0, 0, 1)
            show_life(self, state)
        end

        if state.reaction > 0 then
            state.reaction = math.max(0, state.reaction - seconds)
            local t = 1 - state.reaction / state.reactionSeconds
            local tilt = math.sin(math.pi * t) * 0.3
            self:reference("visual"):set_rotation(0, 0, math.sin(tilt / 2), math.cos(tilt / 2))
        end
    end
}
