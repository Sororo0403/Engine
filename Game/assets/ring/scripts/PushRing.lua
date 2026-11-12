local RADIUS = 6.3
local POOL_SIZE = 16
local ENEMIES = {
    {radius = 0.5, mass = 1, speed = 1.6, points = 100, scale = 0.85},
    {radius = 0.57, mass = 1.3, speed = 1.35, points = 200, scale = 1},
    {radius = 0.85, mass = 3, speed = 0.85, points = 500, scale = 1.45},
}

local function actor(self, name, index)
    return self:reference("pool"):find_child(name .. index)
end

local function length(x, z)
    return math.sqrt(x * x + z * z)
end

local function direction(x, z)
    local distance = length(x, z)
    if distance < 0.0001 then
        return 0, 1, 0
    end
    return x / distance, z / distance, distance
end

local function pressed(input, name)
    return input.actions[name] and input.actions[name].pressed
end

local function cue(self, name)
    self:reference(name):play_audio()
end

-- A compact versioned string fits the existing persistent settings format.
local function records()
    local entries = {}
    local saved = game.pushRingRecords
    if type(saved) ~= "string" or #saved > 163 or saved:sub(1, 3) ~= "R1;"
        or (#saved - 3) % 16 ~= 0 then
        return entries
    end
    for offset = 4, #saved, 16 do
        local score = tonumber(saved:sub(offset, offset + 8))
        local kills = tonumber(saved:sub(offset + 9, offset + 13))
        local combo = tonumber(saved:sub(offset + 14, offset + 15))
        if score and kills and combo and score >= 0 and kills >= 0 and combo >= 0
            and score <= 999999999 and kills <= 99999 and combo <= 99
            and score % 1 == 0 and kills % 1 == 0 and combo % 1 == 0 then
            entries[#entries + 1] = {score = score, kills = kills, combo = combo}
        end
    end
    table.sort(entries, function(a, b) return a.score > b.score end)
    return entries
end

local function show_records(self, entries, rank)
    for i = 1, 10 do
        local entry = entries[i]
        local line = string.format("%02d     —", i)
        if entry then
            line = string.format("%02d     %9d     %4d体     %2d連", i, entry.score, entry.kills, entry.combo)
        end
        if i == rank then
            line = "> " .. line
        end
        self:reference("rank" .. i):set_text(line)
    end
end

local function save_record(self, state)
    local entries = records()
    local entry = {score = state.score, kills = state.kills, combo = state.bestCombo}
    local rank = 1
    while rank <= #entries and entries[rank].score >= entry.score do
        rank = rank + 1
    end
    if rank <= 10 then
        table.insert(entries, rank, entry)
        if #entries > 10 then
            table.remove(entries)
        end
        local saved = {"R1;"}
        for _, item in ipairs(entries) do
            saved[#saved + 1] = string.format("%09d%05d%02d", item.score, item.kills, item.combo)
        end
        game.pushRingRecords = table.concat(saved)
    end
    show_records(self, entries, rank)
    local heading = rank <= 10 and string.format("ランキング %d位！", rank) or "もう一度、リングへ！"
    self:reference("resultHeading"):set_text(heading)
end

local function reset(self, state)
    state.phase = "countdown"
    state.countdown = 2.4
    state.time, state.score, state.kills = 0, 0, 0
    state.bestCombo, state.chains = 0, {}
    state.chainCleanup = 0
    state.spawnTimer, state.spawnCount = 0.5, 0
    state.dashSerial, state.dash, state.cooldown = 0, 0, 0
    state.hitSound, state.noticeTime, state.shake = 0, 0, 0
    state.edge, state.fall = 0, 0
    state.edgeSpent, state.edgeHit = false, false
    state.player = {x = 0, z = 0, vx = 0, vz = 0, dx = 0, dz = 1}
    state.enemies = {}
    for i = 1, POOL_SIZE do
        state.enemies[i] = {active = false}
        actor(self, "Ring Enemy ", i):set_active(false)
        actor(self, "Landing Warning ", i):set_active(false)
    end
    self:reference("hud"):set_active(true)
    self:reference("menu"):set_active(false)
    self:reference("pause"):set_active(false)
    self:reference("result"):set_active(false)
    self:reference("rankings"):set_active(false)
    self:reference("center"):set_text("")
    self:reference("player"):set_active(true)
    self:reference("player"):set_local_position(0, 0.7, 0)
    self:reference("notice"):set_text("")
    self:reference("score"):set_text("0")
    self:reference("status"):set_text("0体  /  最高 0連")
end

local function spawn(self, state)
    local count, slot = 0, nil
    for i, enemy in ipairs(state.enemies) do
        if enemy.active then
            count = count + 1
        elseif not slot then
            slot = i
        end
    end
    if not slot or count >= math.min(13, 3 + math.floor(state.time / 8)) then
        return
    end
    state.spawnCount = state.spawnCount + 1
    local sequence = state.spawnCount
    local kind = 1
    if sequence >= 5 and sequence % 3 == 2 then
        kind = 2
    end
    if sequence >= 11 and sequence % 5 == 1 then
        kind = 3
    end
    local angle = sequence * 2.3999632297
    local enemy = {active = true, kind = kind, sequence = sequence, x = math.sin(angle) * 5.35,
        z = math.cos(angle) * 5.35, vx = 0, vz = 0, dx = 0, dz = 1,
        warning = 0.85, cycle = 0, hit = -1, stun = 0, falling = 0}
    state.enemies[slot] = enemy
    actor(self, "Ring Enemy ", slot):set_mesh_visible(kind == 1)
    actor(self, "Ring Enemy ", slot):find_child("Charger Visual " .. slot):set_active(kind == 2)
    actor(self, "Ring Enemy ", slot):find_child("Heavy Visual " .. slot):set_active(kind == 3)
    local marker = actor(self, "Landing Warning ", slot)
    marker:set_local_position(enemy.x, 0.035, enemy.z)
    marker:set_active(true)
end

local function new_chain(state, key)
    state.chains[key] = {count = 0, expires = state.time + 2.8}
    return key
end

local function knock_out(self, state, enemy)
    enemy.falling = 0.001
    state.kills = math.min(99999, state.kills + 1)
    local chain = enemy.chain and state.chains[enemy.chain]
    local combo = 1
    if chain and state.time <= chain.expires then
        chain.count = chain.count + 1
        combo = chain.count
    end
    state.bestCombo = math.max(state.bestCombo, combo)
    local bonus = 100 * (combo - 1)
    state.score = math.min(999999999, state.score + ENEMIES[enemy.kind].points + bonus)
    state.shake = combo > 1 and 0.23 or 0.15
    state.noticeTime = 0.85
    if combo > 1 then
        state.spawnTimer = math.max(state.spawnTimer, 0.8)
    end
    local notice = combo > 1 and string.format("%d匹まとめ落とし！  +%d", combo, 50 * combo * (combo - 1))
        or string.format("OUT！  +%d", ENEMIES[enemy.kind].points)
    self:reference("notice"):set_text(notice)
    cue(self, combo > 1 and "comboCue" or "outCue")
    self:reference("score"):set_text(tostring(state.score))
    self:reference("status"):set_text(string.format("%d体  /  最高 %d連", state.kills, state.bestCombo))
end

local function move_player(self, state, input, dt)
    local player = state.player
    local dx, dz, distance = direction(input.moveX, input.moveZ)
    if distance > 0.15 and state.dash <= 0 then
        player.dx, player.dz = dx, dz
    end
    state.cooldown = math.max(0, state.cooldown - dt)
    if (pressed(input, "Attack") or input.jumpPressed) and state.cooldown <= 0 then
        state.dash, state.cooldown = 0.18, 0.59
        state.dashSerial = state.dashSerial + 1
        state.dashChain = new_chain(state, "dash" .. state.dashSerial)
        cue(self, "dashCue")
    end
    local mx, mz = 0, 0
    if state.dash > 0 then
        mx, mz = player.dx * 10.5, player.dz * 10.5
    elseif distance > 0.15 then
        local speed = state.cooldown > 0.25 and 2.5 or 4.3
        mx, mz = dx * speed * math.min(1, distance), dz * speed * math.min(1, distance)
    end
    player.x = player.x + (mx + player.vx) * dt
    player.z = player.z + (mz + player.vz) * dt
    player.vx, player.vz = player.vx * math.exp(-5 * dt), player.vz * math.exp(-5 * dt)
end

local function move_enemy(self, state, enemy, dt)
    local spec = ENEMIES[enemy.kind]
    local dx, dz = direction(state.player.x - enemy.x, state.player.z - enemy.z)
    local speed = spec.speed * math.min(1.5, 1 + state.time / 240)
    enemy.stun = math.max(0, enemy.stun - dt)
    enemy.cycle = enemy.cycle + dt
    if enemy.kind == 2 then
        if enemy.cycle < 1.6 then
            enemy.dx, enemy.dz = dx, dz
        elseif enemy.cycle < 2.25 then
            speed = 0
        elseif enemy.cycle < 2.9 then
            dx, dz, speed = enemy.dx, enemy.dz, 7.8
        else
            speed = 0
            if enemy.cycle > 3.65 then
                enemy.cycle = 0
            end
        end
    else
        enemy.dx, enemy.dz = dx, dz
    end
    if enemy.stun > 0 then
        speed = 0
    end
    local charging = enemy.kind == 2 and enemy.cycle >= 2.25 and enemy.cycle < 2.9 and enemy.stun <= 0
    if charging and not enemy.charging and not state.chains[enemy.chain or ""] then
        enemy.charges = (enemy.charges or 0) + 1
        enemy.chain = new_chain(state, "charge" .. enemy.sequence .. "_" .. enemy.charges)
    end
    enemy.charging = charging
    enemy.driveX, enemy.driveZ = charging and dx * speed or 0, charging and dz * speed or 0
    enemy.x = enemy.x + (dx * speed + enemy.vx) * dt
    enemy.z = enemy.z + (dz * speed + enemy.vz) * dt
    local damping = math.exp(-2.8 * dt)
    enemy.vx, enemy.vz = enemy.vx * damping, enemy.vz * damping
end

local function player_contact(self, state, enemy, dt)
    local player, spec = state.player, ENEMIES[enemy.kind]
    local nx, nz, distance = direction(enemy.x - player.x, enemy.z - player.z)
    local reach = 0.5 + spec.radius
    if state.dash > 0 and distance < reach + 0.55 and enemy.hit ~= state.dashSerial
        and nx * player.dx + nz * player.dz > 0.2 then
        enemy.hit = state.dashSerial
        enemy.chain = state.dashChain
        enemy.driveX, enemy.driveZ = 0, 0
        state.cooldown = math.min(state.cooldown, 0.34)
        enemy.vx, enemy.vz = player.dx * 13 / spec.mass, player.dz * 13 / spec.mass
        enemy.stun = 0.5
        state.shake = 0.07
        if state.hitSound <= 0 then
            cue(self, "hitCue")
            state.hitSound = 0.08
        end
    end
    if distance >= reach then
        return
    end
    local overlap = reach - distance
    local share = state.dash > 0 and 0.05 or spec.mass / (spec.mass + 1)
    player.x, player.z = player.x - nx * overlap * share, player.z - nz * overlap * share
    enemy.x, enemy.z = enemy.x + nx * overlap * (1 - share), enemy.z + nz * overlap * (1 - share)
    if state.dash <= 0 and enemy.stun <= 0 then
        local force = enemy.kind == 2 and enemy.cycle >= 2.25 and enemy.cycle < 2.9 and 30 or 12
        player.vx, player.vz = player.vx - nx * force * dt, player.vz - nz * force * dt
        if state.edge > 0.08 and nx * player.x + nz * player.z < 0 then
            state.edgeHit = true
        end
    end
end

local function enemy_contact(state, a, b)
    local sa, sb = ENEMIES[a.kind], ENEMIES[b.kind]
    local nx, nz, distance = direction(b.x - a.x, b.z - a.z)
    local overlap = sa.radius + sb.radius - distance
    if overlap <= 0 then
        return
    end
    local share = sb.mass / (sa.mass + sb.mass)
    a.x, a.z = a.x - nx * overlap * share, a.z - nz * overlap * share
    b.x, b.z = b.x + nx * overlap * (1 - share), b.z + nz * overlap * (1 - share)
    local ax, az = a.vx + (a.driveX or 0), a.vz + (a.driveZ or 0)
    local bx, bz = b.vx + (b.driveX or 0), b.vz + (b.driveZ or 0)
    local forward = ax * nx + az * nz
    local backward = -(bx * nx + bz * nz)
    local closing = forward + backward
    if closing > 1 then
        local impulse = closing * 1.25 / (1 / sa.mass + 1 / sb.mass)
        a.vx, a.vz = ax - nx * impulse / sa.mass, az - nz * impulse / sa.mass
        b.vx, b.vz = bx + nx * impulse / sb.mass, bz + nz * impulse / sb.mass
        a.driveX, a.driveZ, b.driveX, b.driveZ = 0, 0, 0, 0
        a.stun, b.stun = 0.3, 0.3
        if forward >= backward and state.chains[a.chain or ""] then
            b.chain = a.chain
        elseif state.chains[b.chain or ""] then
            a.chain = b.chain
        end
    end
end

local function render(self, state)
    local p = state.player
    local body = self:reference("player")
    body:set_local_position(p.x, 0.7 - state.fall * state.fall * 12, p.z)
    local yaw = math.atan(p.dx, p.dz)
    body:set_rotation(0, math.sin((yaw + math.pi) / 2), 0, math.cos((yaw + math.pi) / 2))
    local stretch = state.dash > 0 and 0.8 or 1 + math.sin(state.time * 12) * 0.025
    if state.edge > 0 and state.fall <= 0 then
        stretch = 0.89 + 0.10 * math.sin(state.time * 72)
    end
    body:set_scale(1 / stretch, stretch, 1 / stretch)
    local arrow = self:reference("aim")
    arrow:set_local_position(p.x + p.dx * 0.9, 0.06, p.z + p.dz * 0.9)
    arrow:set_rotation(0, math.sin(yaw / 2), 0, math.cos(yaw / 2))
    arrow:set_active(state.fall <= 0)
    for i, enemy in ipairs(state.enemies) do
        if enemy.active then
            local visual = actor(self, "Ring Enemy ", i)
            if enemy.warning <= 0 then
                local scale = ENEMIES[enemy.kind].scale
                local crouch = enemy.kind == 2 and enemy.cycle >= 1.6 and enemy.cycle < 2.25
                local height = crouch and 0.7 or 1 + 0.04 * math.sin(state.time * 11 + i)
                if enemy.falling <= 0 and length(enemy.x, enemy.z) > RADIUS - 0.36 then
                    height = 0.96 + 0.08 * math.sin(state.time * 68 + i)
                end
                visual:set_local_position(enemy.x, scale * 0.65 - enemy.falling * enemy.falling * 12, enemy.z)
                visual:set_scale(scale / height, scale * height, scale / height)
                local angle = math.atan(enemy.dx, enemy.dz) + math.pi
                visual:set_rotation(0, math.sin(angle / 2), 0, math.cos(angle / 2))
            end
        end
    end
    local shake = state.shake > 0 and math.sin(state.time * 95) * state.shake * 0.4 or 0
    self:reference("camera"):set_local_position(shake, 13.5, -17)
end

local function step(self, state, input, dt)
    state.time = state.time + dt
    state.chainCleanup = state.chainCleanup - dt
    if state.chainCleanup <= 0 then
        state.chainCleanup = 1
        for key, chain in pairs(state.chains) do
            if chain.expires < state.time then
                state.chains[key] = nil
            end
        end
    end
    state.shake = math.max(0, state.shake - dt)
    state.hitSound = math.max(0, state.hitSound - dt)
    state.noticeTime = math.max(0, state.noticeTime - dt)
    move_player(self, state, input, dt)
    for i, enemy in ipairs(state.enemies) do
        if enemy.active then
            if enemy.warning > 0 then
                enemy.warning = enemy.warning - dt
                if enemy.warning <= 0 then
                    actor(self, "Landing Warning ", i):set_active(false)
                    actor(self, "Ring Enemy ", i):set_active(true)
                end
            elseif enemy.falling > 0 then
                enemy.falling = enemy.falling + dt
                enemy.x, enemy.z = enemy.x + enemy.vx * dt, enemy.z + enemy.vz * dt
                if enemy.falling > 0.65 then
                    enemy.active = false
                    actor(self, "Ring Enemy ", i):set_active(false)
                end
            else
                move_enemy(self, state, enemy, dt)
                player_contact(self, state, enemy, dt)
            end
        end
    end
    for i, a in ipairs(state.enemies) do
        if a.active and a.warning <= 0 and a.falling <= 0 then
            for j = i + 1, POOL_SIZE do
                local b = state.enemies[j]
                if b.active and b.warning <= 0 and b.falling <= 0 then
                    enemy_contact(state, a, b)
                end
            end
            if length(a.x, a.z) > RADIUS + a.kind * 0.04 then
                knock_out(self, state, a)
            end
        end
    end
    state.dash = math.max(0, state.dash - dt)
    local player = state.player
    local nx, nz, distance = direction(player.x, player.z)
    if distance < RADIUS - 0.68 then
        state.edgeSpent = false
    end
    if distance < RADIUS - 0.16 then
        state.edge, state.edgeHit = 0, false
    elseif not state.edgeSpent and distance <= RADIUS + 0.65 then
        state.edgeSpent = true
        state.edge = dt
        player.x, player.z = nx * (RADIUS - 0.035), nz * (RADIUS - 0.035)
        local outward = math.max(0, player.vx * nx + player.vz * nz)
        player.vx, player.vz = player.vx - nx * outward, player.vz - nz * outward
        state.dash = 0
    else
        local usedEdge = state.edge <= 0
        state.edge = state.edge + dt
        if usedEdge or state.edge > 0.36 or state.edgeHit or distance > RADIUS + 0.65 then
            state.phase, state.fall = "falling", 0.001
            cue(self, "fallCue")
        end
    end
    state.spawnTimer = state.spawnTimer - dt
    if state.spawnTimer <= 0 and state.phase == "playing" then
        spawn(self, state)
        state.spawnTimer = math.max(0.65, 2.15 - state.time * 0.018)
    end
end

return {
    start = function(self, state)
        reset(self, state)
        state.phase = "ready"
        self:reference("hud"):set_active(false)
        self:reference("menu"):set_active(true)
        self:reference("rankings"):set_active(true)
        show_records(self, records())
        local volume = game.volumeLevel or 10
        for _, name in ipairs({"dashCue", "hitCue", "outCue", "comboCue", "fallCue", "music"}) do
            local sound = self:reference(name)
            sound:set_audio_volume(sound:get_audio_volume() * volume / 10)
        end
    end,

    message = function(self, state, name)
        if name == "ring_start" and (state.phase == "ready" or state.phase == "result" or state.phase == "paused") then
            reset(self, state)
        elseif name == "ring_resume" and state.phase == "paused" then
            state.phase = state.resumePhase
            self:reference("pause"):set_active(false)
        elseif name == "ring_title" then
            self:load_scene("select")
        end
    end,

    update = function(self, state, seconds, input)
        if seconds <= 0 then
            return
        end
        if pressed(input, "Cancel") and (state.phase == "playing" or state.phase == "countdown") then
            state.resumePhase, state.phase = state.phase, "paused"
            self:reference("pause"):set_active(true)
        end
        if state.phase == "ready" or state.phase == "result" or state.phase == "paused" then
            return
        end
        local dt = math.min(seconds, 0.1)
        if state.phase == "countdown" then
            state.countdown = state.countdown - dt
            self:reference("center"):set_text(tostring(math.max(1, math.ceil(state.countdown))))
            if state.countdown <= 0 then
                state.phase = "playing"
                self:reference("center"):set_text("")
            end
        elseif state.phase == "falling" then
            state.fall = state.fall + dt
            if state.fall >= 0.8 then
                state.phase = "result"
                save_record(self, state)
                self:reference("resultSummary"):set_text(string.format("%d 点    /    %d体撃破    /    最高%d連", state.score, state.kills, state.bestCombo))
                self:reference("hud"):set_active(false)
                self:reference("result"):set_active(true)
                self:reference("rankings"):set_active(true)
                self:reference("player"):set_active(false)
            end
        else
            -- Small fixed upper steps keep fast dashes and knockback contacts stable.
            local steps = math.ceil(dt / (1 / 120))
            for i = 1, steps do
                local subInput = input
                if i > 1 then
                    subInput = {moveX = input.moveX, moveZ = input.moveZ, actions = {}}
                end
                step(self, state, subInput, dt / steps)
                if state.phase ~= "playing" then
                    break
                end
            end
            local ready = state.cooldown <= 0 and "体当たり OK" or "体当たり …"
            self:reference("ability"):set_text(ready)
            if state.edge > 0 then
                self:reference("notice"):set_text("あぶない！ 内側へ戻ろう！")
            elseif state.noticeTime <= 0 then
                self:reference("notice"):set_text("")
            end
        end
        render(self, state)
    end,
}
