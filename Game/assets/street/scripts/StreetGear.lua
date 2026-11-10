local function update_hint(self, state, x, z, inRange)
    if not state.findCanalText then
        return
    end
    local text = state.findCanalText
    if inRange then
        text = state.activateGearText
    elseif (x >= state.farBankX and z >= state.crossingMinZ) or z >= state.crossingMaxZ then
        text = state.findGearText
    elseif x >= state.nearBankX and z >= state.crossingMinZ then
        text = state.jumpText
    end
    if text ~= state.currentHint then
        self:reference("hint"):set_text(text)
        state.currentHint = text
    end
end

return {
    update = function(self, state, _, input)
        if game.streetDeathActive then
            return
        end

        local player = self:reference("player")
        if not player or not player:is_active() then
            return
        end

        local x, y, z = self:get_position()
        local px, py, pz = player:get_position()
        if state.open then
            local hint = self:reference("openHint")
            if state.roofJumpText and hint:is_active() then
                local approaching = py >= state.roofMinY and px >= state.roofNearX
                    and px <= state.roofFarX and math.abs(pz - state.roofGapZ) <= state.roofHintDepth
                local text = approaching and state.roofJumpText or state.roofText
                if text ~= state.currentRoofHint then
                    hint:set_text(text)
                    state.currentRoofHint = text
                end
            end
            return
        end
        local dx, dy, dz = x - px, y - py, z - pz
        local inRange = dx * dx + dz * dz < state.range * state.range and math.abs(dy) < state.height
        update_hint(self, state, px, pz, inRange)
        local attack = input.actions.Attack
        if not inRange or not attack or not attack.pressed then
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
