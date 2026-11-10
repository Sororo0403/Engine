local function show_life(self, state)
    self:reference("life"):set_text("ライフ " .. state.life .. " / " .. state.maximumLife)
end

local function smooth(amount)
    local t = math.min(1, math.max(0, amount))
    return t * t * (3 - 2 * t)
end

local function pose(self, state, tilt, height, eyeHeight, pitch, facingYaw)
    local visual = self:reference("visual")
    local yaw = (facingYaw or state.facingYaw or 0) / 2
    local sy, cy = math.sin(yaw), math.cos(yaw)
    local sr, cr = math.sin(tilt / 2), math.cos(tilt / 2)
    local sp, cp = math.sin((pitch or 0) / 2), math.cos((pitch or 0) / 2)
    -- 体の傾きを主人公の向きに合わせる。
    visual:set_rotation(cy * sp * cr + sy * cp * sr,
        sy * cp * cr - cy * sp * sr, cy * cp * sr - sy * sp * cr,
        cy * cp * cr + sy * sp * sr)
    visual:set_scale(1, height, 1)
    eyeHeight = eyeHeight or 1
    if state.eyeHeight ~= eyeHeight then
        local eyes = self:reference("eyes")
        if eyes then
            eyes:set_scale(1, eyeHeight, 1)
        end
        state.eyeHeight = eyeHeight
    end
end

local function update_facing(state, input, seconds)
    local x, z = input.moveX, input.moveZ
    if x * x + z * z < 0.01 then
        return false
    end
    local yaw = state.facingYaw or 0
    local target = math.atan(x, z)
    local turn = (target - yaw + math.pi) % (2 * math.pi) - math.pi
    if math.abs(turn) < 0.001 then
        return false
    end
    local step = math.min(1, 16 * seconds)
    state.facingYaw = (yaw + turn * step + math.pi) % (2 * math.pi) - math.pi
    return true
end

local function cover(image, x, y, width, height, tint)
    image:set_ui_position(x, y)
    image:set_ui_size(math.max(1, width), math.max(1, height))
    image:set_image_color(tint, tint, tint, width > 0 and height > 0 and 1 or 0)
end

local function iris(self, state, viewport, open)
    local shade = self:reference("deathShade")
    local aperture = self:reference("iris")
    local leftCover, rightCover = self:reference("coverLeft"), self:reference("coverRight")
    local topCover, bottomCover = self:reference("coverTop"), self:reference("coverBottom")
    if open <= 0 or open >= 1 then
        shade:set_image_color(0, 0, 0, open <= 0 and 1 or 0)
        aperture:set_image_color(1, 1, 1, 0)
        for _, image in ipairs({leftCover, rightCover, topCover, bottomCover}) do
            image:set_image_color(0, 0, 0, 0)
        end
        return
    end

    local width, height = viewport.width, viewport.height
    if not state.irisU then
        local camera = scene.get_camera()
        local u, v, depth
        if camera then
            u, v, depth = camera:world_to_viewport(
                state.irisX, state.irisY, state.irisZ, width, height)
            local _, nearClip, farClip = camera:get_camera_lens()
            if depth and (depth < nearClip or depth > farClip) then
                u, v = nil, nil
            end
        end
        state.irisU = math.max(0.03, math.min(0.97, u or 0.5))
        state.irisV = math.max(0.03, math.min(0.97, v or 0.5))
    end
    local cx = state.irisU * width
    local cy = state.irisV * height
    local farX, farY = math.max(cx, width - cx), math.max(cy, height - cy)
    local radius = math.sqrt(farX * farX + farY * farY) * open / 0.90
    local x0, x1 = math.max(0, cx - radius), math.min(width, cx + radius)
    local y0, y1 = math.max(0, cy - radius), math.min(height, cy + radius)

    shade:set_image_color(0, 0, 0, 0)
    cover(aperture, cx - radius, cy - radius, radius * 2, radius * 2, 1)
    cover(leftCover, 0, 0, x0, height, 0)
    cover(rightCover, x1, 0, width - x1, height, 0)
    cover(topCover, x0, 0, x1 - x0, y0, 0)
    cover(bottomCover, x0, y1, x1 - x0, height - y1, 0)
end

local function camera_pose(camera)
    return {position = {camera:get_position()}, rotation = {camera:get_rotation()}}
end

local function lock_camera(self, state, falling)
    local follow = self:reference("followCamera")
    local fixed = self:reference("deathCamera")
    local shot = falling and state.lastGroundCamera or camera_pose(follow)
    fixed:set_position(table.unpack(shot.position))
    fixed:set_rotation(table.unpack(shot.rotation))
    fixed:set_camera_lens(follow:get_camera_lens())
    scene.set_camera(fixed)
    follow:set_active(false)
end

local function restore_camera(self)
    local follow = self:reference("followCamera")
    follow:set_active(true)
    scene.set_camera(follow)
    game.streetCameraReset = true
end

local function background(self, state, amount)
    self:set_background_monochrome(amount, state.deathColorLow, state.deathColorHigh,
        state.deathColorThreshold, state.deathColorSoftness, not state.deathReturned)
end

local function update_motion_sound(self, state, character)
    local previous = state.motionVelocity
    if state.motionGrounded then
        if not character.grounded and character.verticalVelocity > 0 and previous and previous <= 0 then
            local sound = self:reference("jumpSound")
            if sound then
                sound:play_audio()
            end
        elseif character.grounded and previous and previous < -1.5 then
            local sound = self:reference("landSound")
            if sound then
                sound:play_audio()
            end
        end
    end
    state.motionGrounded = state.motionGrounded or character.grounded
    state.motionVelocity = character.verticalVelocity
end

local function finish_damage_flash(self, state)
    if state.damageTint then
        self:reference("visual"):set_mesh_tint(table.unpack(state.damageTint))
        state.damageTint, state.damageFlashDim = nil, nil
    end
end

local function update_damage_flash(self, state)
    if not state.damageTint then
        return
    end
    if state.invulnerable == 0 then
        finish_damage_flash(self, state)
        return
    end

    local elapsed = state.invulnerabilitySeconds - state.invulnerable
    local dim = math.floor(elapsed / 0.12) % 2 == 0
    if dim ~= state.damageFlashDim then
        local r, g, b = table.unpack(state.damageTint)
        local brightness = dim and 0.45 or 1
        self:reference("visual"):set_mesh_tint(r * brightness, g * brightness, b * brightness)
        state.damageFlashDim = dim
    end
end

local function set_pause_available(self, available)
    local receiver = self:reference("pauseReceiver")
    if receiver then
        receiver:set_active(available)
    end
end

local function return_to_title(self)
    game.returningToTitle = true
    self:load_scene("title")
end

local function start_death(self, state, falling)
    finish_damage_flash(self, state)
    set_pause_available(self, false)
    game.streetDeathActive = true
    state.deathTime = 0
    state.motionGrounded, state.motionVelocity = nil, nil
    state.falling = falling
    state.attack = 0
    state.reaction = 0
    state.inputBeforeDeath = self:is_character_input_enabled()
    self:set_character_input_enabled(false)
    lock_camera(self, state, falling)
    state.irisX, state.irisY, state.irisZ = self:get_position()
    if falling then
        state.irisY = state.lastGroundY
    end
    state.irisU, state.irisV = nil, nil
    background(self, state, 0)
end

local function update_fall_pose(self, state, seconds)
    local reaction = smooth((seconds - state.deathReactionDelay) / state.fallReactionSeconds)
    local lift = smooth(reaction / 0.45)
    local drop = smooth((reaction - 0.55) / 0.45)
    local peak = state.lastGroundY + state.fallRise
    local height = state.fallStartY + (peak - state.fallStartY) * lift - state.fallDrop * drop
    local _, y = self:get_position()
    self:reference("visual"):set_local_position(0, height - y, 0)
    pose(self, state, 0.12 * drop, 1 + 0.30 * math.sin(math.pi * reaction), 1 + 0.4 * lift)
end

local function update_death(self, state, seconds, viewport)
    local hold = state.falling and state.fallHold or state.deathHold
    local blackout = hold + state.deathClose
    local opening = blackout + state.deathCovered
    local finish = opening + state.deathReveal
    local t = math.min(finish, state.deathTime + seconds)
    if not state.deathCuePlayed and t >= state.deathReactionDelay then
        local cue = state.falling and "deathFallSound" or "deathContactSound"
        self:reference(cue):play_audio()
        state.deathCuePlayed = true
    end
    viewport = viewport or {width = 1280, height = 720}

    if t < blackout then
        if state.falling then
            update_fall_pose(self, state, t)
        else
            local reaction = smooth((t - state.deathReactionDelay) / state.deathReactionSeconds)
            local bounce = math.sin(reaction * math.pi)
            local facingYaw = reaction > 0 and state.deathFacingYaw or state.facingYaw
            pose(self, state, 0, 1 - 0.34 * bounce, 1 - 0.85 * reaction,
                -0.48 * reaction, facingYaw)
        end
        iris(self, state, viewport, 1 - smooth((t - hold) / state.deathClose))
    elseif not state.deathReturned then
        -- 復帰のフレームは暗幕を閉じた状態で表示する。
        iris(self, state, viewport, 0)
        self:set_position(state.spawnX, state.spawnY, state.spawnZ)
        restore_camera(self)
        state.irisX, state.irisY, state.irisZ = state.spawnX, state.spawnY, state.spawnZ
        state.irisU, state.irisV = nil, nil
        state.life = state.maximumLife
        state.deathReturned = true
        t = blackout
        self:reference("visual"):set_local_position(0, 0, 0)
        pose(self, state, 0, 1)
        show_life(self, state)
    else
        local reveal = smooth((t - opening) / state.deathReveal)
        iris(self, state, viewport, reveal)
        pose(self, state, 0, 1 + 0.12 * math.sin(reveal * math.pi))
    end

    state.deathTime = t
    local monochrome = t < hold
        and smooth((t - state.deathColorHold) / state.deathColorFade)
        or 1 - smooth((t - opening) / state.deathReveal)
    background(self, state, t >= finish and 0 or monochrome)
    if t >= finish then
        pose(self, state, 0, 1)
        self:set_character_input_enabled(state.inputBeforeDeath)
        state.inputBeforeDeath = nil
        state.invulnerable = state.invulnerabilitySeconds
        game.streetDeathActive = false
        set_pause_available(self, true)
        state.deathTime = nil
        state.deathReturned = nil
        state.deathCuePlayed = nil
        state.falling = nil
    end
end

local function update_attack(self, state, seconds, input)
    state.attackCooldown = math.max(0, state.attackCooldown - seconds)
    local wasAttacking = state.attack > 0
    state.attack = math.max(0, state.attack - seconds)

    local action = input.actions.Attack
    if action and action.pressed and state.attackCooldown == 0 then
        state.attack = state.attackSeconds
        state.attackCooldown = state.attackCooldownSeconds
        self:reference("attackSound"):play_audio()
    end
    if state.attack == 0 then
        if wasAttacking and state.reaction == 0 then
            pose(self, state, 0, 1)
        end
        return
    end

    local x, y, z = self:get_position()
    local struck = false
    for _, enemy in ipairs(scene.find_all_by_tag("street_enemy")) do
        local ex, ey, ez = enemy:get_position()
        local dx, dy, dz = ex - x, ey - y, ez - z
        local distance = math.sqrt(dx * dx + dy * dy + dz * dz)
        if distance < state.attackDistance then
            local hit
            if distance > 0.001 then
                hit = scene.raycast(x, y, z, dx / distance, dy / distance, dz / distance, distance)
            end
            if not hit or hit.object == enemy then
                enemy:destroy()
                struck = true
            end
        end
    end

    if struck then
        self:reference("hitSound"):play_audio()
    end

    if state.reaction == 0 then
        local progress = 1 - state.attack / state.attackSeconds
        pose(self, state, 0, 1 - 0.18 * math.sin(math.pi * progress))
    end
end

local function on_contact(self, state, other)
    if not other or other:get_tag() ~= "street_enemy" or state.invulnerable > 0 or state.deathTime or state.completed then
        return
    end

    state.life = state.life - 1
    state.invulnerable = state.invulnerabilitySeconds
    state.reaction = state.reactionSeconds
    if state.life == 0 then
        local x, _, z = self:get_position()
        local ex, _, ez = other:get_position()
        local dx, dz = ex - x, ez - z
        state.deathFacingYaw = dx * dx + dz * dz > 0.0001 and math.atan(dx, dz) or state.facingYaw
        start_death(self, state, false)
    else
        state.damageTint = {self:reference("visual"):get_mesh_tint()}
        update_damage_flash(self, state)
        self:reference("damageSound"):play_audio()
    end
    show_life(self, state)
end

return {
    start = function(self, state)
        game.streetDeathActive = false
        state.spawnX, state.spawnY, state.spawnZ = self:get_position()
        state.lastGroundY = state.spawnY
        state.lastGroundCamera = camera_pose(self:reference("followCamera"))
    end,

    message = function(self, state, name)
        if name == "courtyard" or name == "roof" then
            state.spawnX, state.spawnY, state.spawnZ = self:reference(name):get_position()
        elseif name == "street_result_ready" then
            state.resultReady = true
        elseif state.completed and name == "street_retry" then
            self:load_scene("restart")
        elseif name == "pause_title" or (state.completed and name == "street_title") then
            return_to_title(self)
        elseif name == "street_clear" then
            set_pause_available(self, false)
            finish_damage_flash(self, state)
            state.completed = true
            self:set_character_input_enabled(false)
            local camera = scene.get_camera()
            if camera then
                local cx, _, cz = camera:get_position()
                local x, _, z = self:get_position()
                state.facingYaw = math.atan(cx - x, cz - z)
            end
            pose(self, state, 0, 1)
            for _, enemy in ipairs(scene.find_all_by_tag("street_enemy")) do
                enemy:send_message("street_clear")
            end
        end
    end,

    collision_enter = on_contact,
    collision_stay = on_contact,

    update = function(self, state, seconds, input)
        if state.deathTime then
            update_death(self, state, seconds, input.viewport)
            return
        end

        if state.completed and not state.resultReady then
            return
        end

        local restart, cancel = input.actions.Restart, input.actions.Cancel
        if restart and restart.pressed then
            self:load_scene("restart")
            return
        end
        if cancel and cancel.pressed and (state.completed or not self:reference("pauseReceiver")) then
            return_to_title(self)
            return
        end
        if state.completed then
            return
        end

        state.invulnerable = math.max(0, state.invulnerable - seconds)
        update_damage_flash(self, state)

        if input.character and state.respawnCount ~= input.character.respawnCount then
            state.respawnCount = input.character.respawnCount
            finish_damage_flash(self, state)
            state.motionGrounded, state.motionVelocity = nil, nil
            state.life = state.maximumLife
            state.invulnerable = state.invulnerabilitySeconds
            state.reaction = 0
            state.attack = 0
            pose(self, state, 0, 1)
            show_life(self, state)
        end

        if input.character then
            local _, y = self:get_position()
            if y <= state.fallY then
                state.fallStartY = y
                state.life = 0
                start_death(self, state, true)
                show_life(self, state)
                return
            end
            update_motion_sound(self, state, input.character)
            if input.character.grounded then
                state.lastGroundY = y
                state.lastGroundCamera = camera_pose(self:reference("followCamera"))
            end
        end

        local turned = self:is_character_input_enabled() and update_facing(state, input, seconds)
        update_attack(self, state, seconds, input)

        if state.reaction > 0 then
            state.reaction = math.max(0, state.reaction - seconds)
            local t = 1 - state.reaction / state.reactionSeconds
            local tilt = math.sin(math.pi * t) * 0.3
            pose(self, state, tilt, 1, 1 - 0.65 * math.sin(math.pi * t))
        elseif turned and state.attack == 0 then
            pose(self, state, 0, 1)
        end
    end
}
