local function restore(tile, player, state)
    local px, py, pz = player:get_position()
    local x, y, z = tile:world_to_local(px, py, pz)
    if not tile:is_active() and math.abs(x) <= state.halfWidth + 0.4
        and math.abs(z) <= state.halfLength + 0.4 and y > -0.93 and y < 0.71 then
        return false
    end
    tile:set_active(true)
    tile:set_mesh_tint(1, 1, 1)
    return true
end

return {
    update = function(self, state, seconds, input)
        state.tiles = state.tiles or {}
        local running = self:reference("barriers"):is_active()
        local player = self:reference("player")
        for _, name in ipairs(state.tileNames) do
            local tile = self:reference(name)
            local timing = state.tiles[name]
            if not running then
                if timing and restore(tile, player, state) then
                    state.tiles[name] = nil
                end
            elseif timing then
                timing.seconds = timing.seconds - seconds
                if timing.seconds <= 0 then
                    if timing.open then
                        if restore(tile, player, state) then
                            state.tiles[name] = nil
                        end
                    else
                        tile:set_active(false)
                        timing.open, timing.seconds = true, state.restoreSeconds
                    end
                elseif not timing.open then
                    local flash = math.floor(timing.seconds / 0.13) % 2 == 0
                    tile:set_mesh_tint(1, flash and 0.55 or 0.86, flash and 0.22 or 0.7)
                end
            elseif input.character and input.character.ground == tile then
                state.tiles[name] = {seconds = state.warningSeconds, open = false}
            end
        end
    end
}
