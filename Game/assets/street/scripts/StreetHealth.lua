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

local function cover(image, x, y, width, height, tint)
    image:set_ui_position(x, y)
    image:set_ui_size(math.max(1, width), math.max(1, height))
    image:set_image_color(tint, tint, tint, width > 0 and height > 0 and 1 or 0)
end

local function iris(self, viewport, open)
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
    local x, y, z = self:reference("visual"):get_position()
    local camera = scene.get_camera()
    local u, v
    if camera then
        u, v = camera:world_to_viewport(x, y - 0.15, z, width, height)
    end
    local cx = math.max(0, math.min(1, u or 0.5)) * width
    local cy = math.max(0, math.min(1, v or 0.5)) * height
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

local function update_death(self, state, seconds, viewport)
    local blackout = state.deathHold + state.deathClose
    local opening = blackout + state.deathCovered
    local finish = opening + state.deathReveal
    local t = math.min(finish, state.deathTime + seconds)
    viewport = viewport or {width = 1280, height = 720}

    if t < blackout then
        local reaction = smooth((t - state.deathReactionDelay) / state.deathReactionSeconds)
        local bounce = math.sin(reaction * math.pi)
        pose(self, -0.48 * reaction, 1 - 0.34 * bounce)
        iris(self, viewport, 1 - smooth((t - state.deathHold) / state.deathClose))
    elseif not state.deathReturned then
        -- 復帰のフレームは暗幕を閉じた状態で表示する。
        iris(self, viewport, 0)
        self:set_position(state.spawnX, state.spawnY, state.spawnZ)
        state.life = state.maximumLife
        state.deathReturned = true
        t = blackout
        pose(self, 0, 1)
        show_life(self, state)
    else
        local reveal = smooth((t - opening) / state.deathReveal)
        iris(self, viewport, reveal)
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
            pose(self, 0, 1)
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
        pose(self, 0, 1 - 0.18 * math.sin(math.pi * progress))
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
        state.deathTime = 0
        state.attack = 0
        state.reaction = 0
        state.moveSpeed, state.jumpSpeed = self:get_character_speeds()
        self:set_character_speeds(0.001, 0.001)
    else
        self:reference("damageSound"):play_audio()
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
        elseif name == "street_clear" then
            state.completed = true
            self:set_character_speeds(0.001, 0.001)
            pose(self, 0, 1)
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

        local restart, cancel = input.actions.Restart, input.actions.Cancel
        if restart and restart.pressed then
            self:load_scene("restart")
            return
        end
        if cancel and cancel.pressed then
            self:load_scene("title")
            return
        end
        if state.completed then
            local confirm = input.actions.Confirm
            if confirm and confirm.pressed then
                self:load_scene("title")
            end
            return
        end

        state.invulnerable = math.max(0, state.invulnerable - seconds)

        if input.character and state.respawnCount ~= input.character.respawnCount then
            state.respawnCount = input.character.respawnCount
            state.life = state.maximumLife
            state.invulnerable = state.invulnerabilitySeconds
            state.reaction = 0
            state.attack = 0
            pose(self, 0, 1)
            show_life(self, state)
        end

        update_attack(self, state, seconds, input)

        if state.reaction > 0 then
            state.reaction = math.max(0, state.reaction - seconds)
            local t = 1 - state.reaction / state.reactionSeconds
            local tilt = math.sin(math.pi * t) * 0.3
            pose(self, tilt, 1)
        end
    end
}
