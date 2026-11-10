return {
    start = function(self, state)
        state.u, state.v = self:get_mesh_uv_offset()
        state.phase = 0
    end,

    update = function(self, state, seconds)
        state.phase = (state.phase + state.speed * seconds) % 1
        self:set_mesh_uv_offset(state.u, state.v + state.phase)
    end
}
