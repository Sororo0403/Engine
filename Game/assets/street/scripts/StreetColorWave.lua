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
        state.x, state.y, state.z = self:reference("origin"):get_position()
        state.edgeWidth = state.edgeWidth or 0.45
        local objects, truncated = scene.find_all_by_tag(state.targetTag)
        assert(not truncated, "Too many targets for one street color wave")
        state.pending = {}
        for _, object in ipairs(objects) do
            state.pending[#state.pending + 1] = object:get_id()
            object:set_mesh_radial_reveal(state.x, state.y, state.z,
                radius_keys[1][2], state.edgeWidth)
        end
    end,

    update = function(self, state, seconds)
        if #state.pending == 0 then
            return
        end
        state.elapsed = state.elapsed + seconds
        if state.elapsed <= radius_keys[1][1] then
            return
        end

        local radius = wave_radius(state.elapsed)
        local finished = state.elapsed >= radius_keys[#radius_keys][1]
        for _, id in ipairs(state.pending) do
            local object = scene.find_id(id)
            if object then
                object:set_mesh_radial_reveal(state.x, state.y, state.z,
                    radius, finished and 0 or state.edgeWidth)
            end
        end
        if finished then
            state.pending = {}
        end
    end
}
