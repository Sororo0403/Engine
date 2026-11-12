return {
    message = function(self, state, name)
        if self:reference(name) then
            state.checkpointCamera = name
        end
    end,

    update = function(self, state, _, input)
        local character = input.character
        if not character or character.respawnCount == (state.respawnCount or 0) then
            return
        end

        state.respawnCount = character.respawnCount
        local camera = self:reference(state.checkpointCamera or "start")
        if camera then
            scene.set_camera(camera)
        end
    end
}
