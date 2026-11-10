local function canal_bridge_hint(self, x, z)
    local bridge = self:reference("canalBridge")
    local winch = self:reference("canalWinch")
    if not bridge or not winch then
        return nil
    end
    local wx, _, wz = winch:get_position()
    if math.abs(z - wz) > 1.1 then
        return nil
    end
    if bridge:is_active() then
        if x >= -3.5 and x <= 5 then
            return "開いた橋を歩いて運河を渡ろう"
        end
    elseif math.abs(x - wx) <= 1.25 then
        return "巻き上げ機を操作して運河に橋を架ける ［E / X］"
    end
end

local function market_supply_hint(self, x, z)
    local crate = self:reference("supplyCrate")
    local supply = self:reference("marketSupply")
    if not crate or not supply or not supply:is_active() then
        return nil
    end
    local cx, _, cz = supply:get_position()
    if math.abs(x - cx) > 2.7 or z < cz - 6 or z > cz + 1.5 then
        return nil
    end
    if crate:is_active() then
        return "箱をたたいて物置を開ける ［J / X］"
    end
    return "物置の救急箱でライフを1回復"
end

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
        local marker = self:reference("canalMarker")
        if marker and state.canalApproachText and state.canalMarkerDepth then
            local _, _, markerZ = marker:get_position()
            if math.abs(z - markerZ) > state.canalMarkerDepth then
                text = state.canalApproachText
            end
        end
        local step = self:reference("canalStep")
        if step and state.canalStepText and state.canalStepHintDepth then
            local _, _, stepZ = step:get_position()
            if math.abs(z - stepZ) <= state.canalStepHintDepth then
                text = state.canalStepText
            end
        end
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
    if not inRange then
        text = canal_bridge_hint(self, x, z) or market_supply_hint(self, x, z) or text
        local practice = self:reference("jumpPractice")
        if practice then
            local px, _, pz = practice:get_position()
            if (x - px) ^ 2 + (z - pz) ^ 2 < 12 then
                text = "荷箱を跳んでカフェの右へ ［Space / A］"
            end
        end
        local longJump = self:reference("longJumpPractice")
        if longJump then
            local px, _, pz = longJump:get_position()
            if math.abs(x - px) < 1.15 and math.abs(z - pz) < 1.6 then
                text = "手前の台に上がり、矢印から長押しで跳ぼう ［Space / A］"
            end
        end
    end
    if text ~= state.currentHint then
        self:reference("hint"):set_text(text)
        state.currentHint = text
    end
end

local function try_break_supply_crate(self, player, px, py, pz)
    local crate = self:reference("supplyCrate")
    if not crate or not crate:is_active() then
        return
    end
    local x, y, z = crate:get_position()
    local dx, dy, dz = x - px, y - py, z - pz
    local distance = math.sqrt(dx * dx + dy * dy + dz * dz)
    if distance <= 0.01 or distance > 3 then
        return
    end
    local hit = scene.raycast(px, py, pz, dx / distance, dy / distance, dz / distance,
        distance, nil, false, player)
    if hit and hit.object == crate then
        crate:set_active(false)
        self:play_audio()
    end
end

local function try_open(self, state, player, px, py, pz)
    local x, y, z = self:get_position()
    local dx, dy, dz = x - px, y - py, z - pz
    local flatDistance = dx * dx + dz * dz
    if flatDistance >= state.range * state.range or math.abs(dy) >= state.height then
        return
    end

    local distance = math.sqrt(flatDistance + dy * dy)
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

return {
    start = function(self, state)
        if state.findCanalText and state.introRadius then
            state.introText = self:reference("hint"):get_text()
        end
    end,

    message = function(self, state, name, data, sender)
        if name == "street_enemy_alert" and state.enemyHintText then
            state.enemyHintRemaining = state.enemyHintSeconds
        elseif name == "street_attack_started" and state.attackFromPlayer
            and not game.streetDeathActive then
            local player = self:reference("player")
            if player and player == sender and player:is_active() then
                try_break_supply_crate(self, player, data.x, data.y, data.z)
                if not state.open then
                    try_open(self, state, player, data.x, data.y, data.z)
                end
            end
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
                local onRoof = py >= state.roofMinY and pz >= (state.roofEntryZ or -math.huge)
                local approaching = onRoof and px >= state.roofNearX
                    and px <= state.roofFarX and math.abs(pz - state.roofGapZ) <= state.roofHintDepth
                local text = approaching and state.roofJumpText or state.roofText
                if onRoof and state.roofReturnText and px >= state.roofReturnMinX
                    and px <= state.roofReturnMaxX
                    and math.abs(pz - state.roofGapZ) <= state.roofReturnDepth then
                    text = state.roofReturnText
                end
                local stair = self:reference("balconyStair")
                if onRoof and stair and state.balconyStairText and px >= state.roofNearX
                    and px <= state.roofFarX then
                    local _, _, stairZ = stair:get_position()
                    local _, _, stairDepth = stair:get_scale()
                    if math.abs(pz - stairZ) <= stairDepth then
                        text = state.balconyStairText
                    end
                end
                if not onRoof and state.stairsText then
                    text = state.stairsText
                    local balcony = self:reference("catchBalcony")
                    if balcony and state.balconyText then
                        local bx, by, bz = balcony:get_position()
                        local sx, sy, sz = balcony:get_scale()
                        if py >= by + sy and math.abs(px - bx) <= sx and math.abs(pz - bz) <= sz then
                            text = state.balconyText
                        end
                    end
                end
                if state.canalReturnText and px >= state.farBankX
                    and pz >= state.crossingMinZ and pz <= state.crossingMaxZ then
                    local canalReturn = self:reference("canalReturnMarker")
                    if canalReturn then
                        local bx, by, bz = canalReturn:get_position()
                        if px <= bx + 1.1 and math.abs(py - by) < 1.5
                            and math.abs(pz - bz) <= state.canalMarkerDepth then
                            text = state.canalReturnText
                        end
                    end
                    local step = self:reference("canalStep")
                    if step and state.canalStepReturnText and state.canalStepHintDepth then
                        local sx, sy, sz = step:get_position()
                        if px <= sx + 3 and math.abs(py - sy) < 1.6
                            and math.abs(pz - sz) <= state.canalStepHintDepth then
                            text = state.canalStepReturnText
                        end
                    end
                end
                text = canal_bridge_hint(self, px, pz) or text
                local returnMarker = self:reference("courtyardReturnMarker")
                if onRoof and returnMarker and state.courtyardReturnText then
                    local mx, my, mz = returnMarker:get_position()
                    if math.abs(px - mx) <= 2.5 and math.abs(py - my) <= 1.5
                        and math.abs(pz - mz) <= 2 then
                        text = state.courtyardReturnText
                    end
                end
                if game.streetGoalWaiting and state.finishPrompt then
                    text = state.finishPrompt
                    if game.streetRemainingFlowers then
                        text = text .. "   " .. game.streetRemainingFlowers
                    end
                end
                if text ~= state.currentRoofHint then
                    hint:set_text(text)
                    state.currentRoofHint = text
                end
            end
            return
        end
        local x, y, z = self:get_position()
        local dx, dy, dz = x - px, y - py, z - pz
        local inRange = dx * dx + dz * dz < state.range * state.range and math.abs(dy) < state.height
        update_hint(self, state, px, pz, inRange, seconds)
        local attack = input.actions.Attack
        if not state.attackFromPlayer and inRange and attack and attack.pressed then
            try_open(self, state, player, px, py, pz)
        end
    end
}
