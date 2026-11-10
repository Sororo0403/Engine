return {
    start = function(self, state)
        state.baseVolume = self:get_audio_volume()
        self:set_audio_volume(0)
    end,

    message = function(self, state, name)
        if state.departing then
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
        local progress = state.elapsed / duration
        local volume = state.departing and state.fromVolume * (1 - progress)
            or state.baseVolume * progress
        self:set_audio_volume(volume)
    end
}
