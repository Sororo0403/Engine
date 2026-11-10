local function gain()
    return (game.volumeLevel or 10) / 10
end

local cue_names = {"confirmSound", "backSound", "adjustSound"}

local function apply_cue_volume(self, state)
    for i, name in ipairs(cue_names) do
        self:reference(name):set_audio_volume(state.cueVolumes[i] * gain())
    end
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
        local level = game.volumeLevel
        if type(level) ~= "number" or level < 0 or level > 10 or level % 1 ~= 0 then
            game.volumeLevel = nil
        end
        state.baseVolume = self:get_audio_volume()
        self:set_audio_volume(0)
        show_volume(self)
        state.cueVolumes = {}
        for i, name in ipairs(cue_names) do
            state.cueVolumes[i] = self:reference(name):get_audio_volume()
        end
        apply_cue_volume(self, state)
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
            apply_cue_volume(self, state)
            return
        end
        if name ~= "start_departure" and name ~= "quit_departure" then
            return
        end
        if name == "start_departure" then
            for _, buttonName in ipairs({"startButton", "configButton", "quitButton"}) do
                local button = self:reference(buttonName)
                if button then
                    button:set_interactable(false)
                end
            end
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
