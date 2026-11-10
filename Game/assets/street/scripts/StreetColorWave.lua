local radius_keys = {
    {0.716667, -2}, {0.92, 0}, {0.972632, 5}, {1.02, 14},
    {1.077895, 38}, {1.151579, 90}, {1.22, 180}
}

local function wave_radius(time)
    if time <= radius_keys[1][1] then
        return radius_keys[1][2]
    end
    for i = 2, #radius_keys do
        local next = radius_keys[i]
        if time <= next[1] then
            local previous = radius_keys[i - 1]
            local t = (time - previous[1]) / (next[1] - previous[1])
            return previous[2] + (next[2] - previous[2]) * t
        end
    end
    return radius_keys[#radius_keys][2]
end

return {
    start = function(self, state)
        local x, y, z = self:reference("origin"):get_position()
        local objects, truncated = scene.find_all_by_tag(state.targetTag)
        assert(not truncated, "Too many targets for one street color wave")
        state.pending = {}
        for _, object in ipairs(objects) do
            local px, py, pz = object:get_position()
            local dx, dy, dz = px - x, py - y, pz - z
            local r, g, b = object:get_mesh_tint()
            state.pending[#state.pending + 1] = {
                object:get_id(), math.sqrt(dx * dx + dy * dy + dz * dz), r, g, b
            }
            object:set_mesh_tint(0, 0, 0)
        end
    end,

    update = function(self, state, seconds)
        if #state.pending == 0 then
            return
        end
        state.elapsed = state.elapsed + seconds
        if state.elapsed < 0.92 then
            return
        end

        local radius = wave_radius(state.elapsed)
        local pending = {}
        for _, target in ipairs(state.pending) do
            local distance = target[2]
            local t = math.min(1, math.max(0, (radius - distance + 0.45) / 0.9))
            if t > 0 then
                local object = scene.find_id(target[1])
                if object then
                    local blend = t * t * (3 - 2 * t)
                    object:set_mesh_tint(target[3] * blend, target[4] * blend, target[5] * blend)
                end
            end
            if t < 1 then
                pending[#pending + 1] = target
            end
        end
        state.pending = pending
    end
}
