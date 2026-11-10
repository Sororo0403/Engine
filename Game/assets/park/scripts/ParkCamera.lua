local function smoothstep(value)
    local t = math.max(0, math.min(1, value))
    return t * t * (3 - 2 * t)
end

return {
    start = function(self, state)
        state.normalFov = self:get_camera_lens()
        state.fov = state.normalFov
    end,

    update = function(self, state, seconds)
        local x, _, z = self:reference("player"):get_position()
        local _, _, switchZ = self:reference("westSwitch"):get_position()
        local nearSwitch = smoothstep((4 - math.abs(z - switchZ)) / 2.4)
        local onWestSide = smoothstep((0.75 - x) / 2.2)
        local target = state.normalFov + (state.jumpFov - state.normalFov) * nearSwitch * onWestSide
        local fov = target + (state.fov - target) * math.exp(-7 * seconds)
        if math.abs(fov - target) < 0.05 then
            fov = target
        end
        if fov ~= state.fov then
            state.fov = fov
            self:set_camera_fov(fov)
        end
    end
}
