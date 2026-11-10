local function update_hint(self, state, x, z, inRange, seconds)
    if not state.findCanalText then
        return
    end
    local text = state.findCanalText
    local enemyAlert = state.enemyHintRemaining and state.enemyHintRemaining > 0
    if enemyAlert then
        state.enemyHintRemaining = state.enemyHintRemaining - seconds
    end
    if inRange then
        text = state.activateGearText
    elseif enemyAlert then
        text = state.enemyHintText
    elseif (x >= state.farBankX and z >= state.crossingMinZ) or z >= state.crossingMaxZ then
        text = state.findGearText
    elseif x >= state.nearBankX and z >= state.crossingMinZ then
        text = state.jumpText
    elseif state.introRadius and not state.introFinished then
        state.introStartX = state.introStartX or x
        state.introStartZ = state.introStartZ or z
        local dx, dz = x - state.introStartX, z - state.introStartZ
        if dx * dx + dz * dz >= state.introRadius * state.introRadius then
            state.introFinished = true
        else
            text = state.introText
        end
    end
    if text ~= state.currentHint then
        self:reference("hint"):set_text(text)
        state.currentHint = text
    end
end

return {
    start = function(self, state)
        if state.findCanalText and state.introRadius then
            state.introText = self:reference("hint"):get_text()
        end
    end,

    message = function(_, state, name)
        if name == "street_enemy_alert" and state.enemyHintText then
            state.enemyHintRemaining = state.enemyHintSeconds
        end
    end,

    update = function(self, state, seconds, input)
        if game.streetDeathActive then
            state.enemyHintRemaining = nil
            return
        end

        local player = self:reference("player")
        if not player or not player:is_active() then
            return
        end

        local x, y, z = self:get_position()
        local px, py, pz = player:get_position()
        if state.open then
            if state.shutterClearY and not state.shutterReleased then
                local shutterVisual = self:reference("shutterVisual")
                if shutterVisual then
                    local _, centerY = shutterVisual:get_position()
                    local _, halfHeight = shutterVisual:get_scale()
                    if centerY - halfHeight >= state.shutterClearY then
                        self:reference("shutter"):set_active(false)
                        state.shutterReleased = true
                    end
                end
                if shutterVisual and not state.shutterReleased then
                    return
                end
            end
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
        update_hint(self, state, px, pz, inRange, seconds)
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
        local openHint = self:reference("openHint")
        local shutterVisual = self:reference("shutterVisual")
        if shutterVisual and state.shutterClearY then
            shutterVisual:play_animation()
            if state.shutterOpeningText and state.roofJumpText then
                openHint:set_text(state.shutterOpeningText)
            end
        else
            self:reference("shutter"):set_active(false)
        end
        self:reference("goal"):set_active(true)
        self:reference("hint"):set_active(false)
        openHint:set_active(true)
        self:reference("visual"):play_animation()
        self:play_audio()
    end
}
