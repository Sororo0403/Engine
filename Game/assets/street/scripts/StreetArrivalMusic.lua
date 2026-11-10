local function ease(time, first, last)
    if last <= first then
        return time >= last and 1 or 0
    end
    local t = math.max(0, math.min(1, (time - first) / (last - first)))
    return t * t * (3 - 2 * t)
end

return {
    start = function(self, state)
        state.baseVolume = self:get_audio_volume()
        self:set_audio_volume(state.baseVolume * state.quietGain)
    end,

    update = function(self, state, seconds)
        if state.elapsed >= state.restoreAt then
            return
        end

        state.elapsed = math.min(state.restoreAt, state.elapsed + seconds)
        local hush = ease(state.elapsed, state.release - 0.12, state.release - 0.04)
        local quiet = state.quietGain + (state.hushGain - state.quietGain) * hush
        local release = ease(state.elapsed, state.release + 0.12, state.restoreAt)
        self:set_audio_volume(state.baseVolume * (quiet + (1 - quiet) * release))
    end
}
