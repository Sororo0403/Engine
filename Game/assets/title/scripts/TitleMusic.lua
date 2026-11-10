local function gain()
    return (game.volumeLevel or 10) / 10
end

local function show_volume(self)
    self:reference("volumeLabel"):set_text((game.volumeLevel or 10) * 10 .. "%")
end

local function apply_volume(self, state)
    local progress = state.elapsed / math.max(0.01, state.duration)
    local volume = state.departing and state.fromVolume * (1 - progress)
        or state.baseVolume * gain() * progress
    self:set_audio_volume(volume)
end

return {
    start = function(self, state)
        state.baseVolume = self:get_audio_volume()
        self:set_audio_volume(0)
        show_volume(self)
    end,

    message = function(self, state, name)
        if state.departing then
            return
        end
        if name == "volume_down" or name == "volume_up" then
            local step = name == "volume_down" and -1 or 1
            game.volumeLevel = math.min(10, math.max(0, (game.volumeLevel or 10) + step))
            show_volume(self)
            apply_volume(self, state)
            return
        end
        if name ~= "start_departure" and name ~= "quit_departure" then
            return
        end
        state.departing = true
        state.elapsed = 0
        state.fromVolume = self:get_audio_volume()
        state.duration = name == "start_departure" and state.startFade or state.quitFade
    end,

    update = function(self, state, seconds)
        local duration = math.max(0.01, state.duration)
        if state.elapsed >= duration then
            return
        end
        state.elapsed = math.min(duration, state.elapsed + seconds)
        apply_volume(self, state)
    end
}
