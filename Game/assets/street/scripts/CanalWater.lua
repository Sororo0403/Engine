return {
    start = function(self, state)
        state.u, state.v = self:get_mesh_uv_offset()
        state.phase = 0
    end,

    update = function(self, state, seconds)
        state.phase = (state.phase + state.speed * seconds) % 1
        self:set_mesh_uv_offset(state.u, state.v + state.phase)

        local player = self:reference("player")
        local splash = self:reference("splash")
        if not player or not splash then
            return
        end

        local px, py, pz = player:get_position()
        local x, y, z = self:world_to_local(px, py - state.feetOffset, pz)
        local entered = state.previousHeight and state.previousHeight > 0 and y <= 0
        state.previousHeight = y
        if entered and math.abs(x) <= 1 and math.abs(z) <= 1 and not game.streetDeathActive then
            splash:set_position(self:local_to_world(x, 1, z))
            splash:emit_particles(22)
            splash:play_audio()
        end
    end
}
