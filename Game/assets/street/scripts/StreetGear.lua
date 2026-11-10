return {
    update = function(self, state, _, input)
        local attack = input.actions.Attack
        if state.open or game.streetDeathActive or not attack or not attack.pressed then
            return
        end

        local player = self:reference("player")
        if not player or not player:is_active() then
            return
        end

        local x, y, z = self:get_position()
        local px, py, pz = player:get_position()
        local dx, dy, dz = x - px, y - py, z - pz
        if dx * dx + dz * dz >= state.range * state.range or math.abs(dy) >= state.height then
            return
        end

        local distance = math.sqrt(dx * dx + dy * dy + dz * dz)
        if distance > 0.01 then
            local hit = scene.raycast(px, py, pz, dx / distance, dy / distance, dz / distance,
                distance, nil, false, player)
            if hit and hit.object ~= self:reference("pedestal") and hit.object ~= self:reference("wheel") then
                return
            end
        end

        state.open = true
        self:reference("shutter"):set_active(false)
        self:reference("goal"):set_active(true)
        self:reference("hint"):set_active(false)
        self:reference("openHint"):set_active(true)
        self:reference("visual"):play_animation()
        self:play_audio()
    end
}
