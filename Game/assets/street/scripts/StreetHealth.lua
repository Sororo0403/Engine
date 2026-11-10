local function show_life(self, state)
    self:reference("life"):set_text("ライフ " .. state.life .. " / " .. state.maximumLife)
end

local function on_contact(self, state, other)
    if not other or other:get_tag() ~= "street_enemy" or state.invulnerable > 0 then
        return
    end

    state.life = state.life - 1
    state.invulnerable = state.invulnerabilitySeconds
    state.reaction = state.reactionSeconds
    if state.life == 0 then
        self:set_position(state.spawnX, state.spawnY, state.spawnZ)
        state.life = state.maximumLife
        state.reaction = 0
        self:reference("visual"):set_rotation(0, 0, 0, 1)
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
