local function volume_level(state)
    return state.volumeDraft or game.volumeLevel or 10
end

local cue_names = {"confirmSound", "backSound", "adjustSound"}

local function apply_cue_volume(self, state)
    for i, name in ipairs(cue_names) do
        self:reference(name):set_audio_volume(state.cueVolumes[i] * volume_level(state) / 10)
    end
end

local function show_volume(self, state)
    self:reference("volumeLabel"):set_text(volume_level(state) * 10 .. "%")
end

local function apply_motion(self, state)
    local enabled = state.motionDraft
    if enabled == nil then
        enabled = game.backgroundMotion ~= false
    end
    local camera = self:reference("idleCamera")
    if camera then
        camera:set_animation_clip_paused(not enabled)
        self:reference("motionOnButton"):set_active(enabled)
        self:reference("motionOffButton"):set_active(not enabled)
    end
end

local function apply_volume(self, state)
    local progress = state.elapsed / math.max(0.01, state.duration)
    local volume = state.departing and state.fromVolume * (1 - progress)
        or state.baseVolume * volume_level(state) / 10 * progress
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
        show_volume(self, state)
        state.cueVolumes = {}
        for i, name in ipairs(cue_names) do
            state.cueVolumes[i] = self:reference(name):get_audio_volume()
        end
        apply_cue_volume(self, state)
        apply_motion(self, state)
    end,

    message = function(self, state, name)
        if state.departing then
            return
        end
        if name == "volume_down" or name == "volume_up" then
            local step = name == "volume_down" and -1 or 1
            state.volumeDraft = math.min(10, math.max(0, volume_level(state) + step))
            show_volume(self, state)
            apply_volume(self, state)
            apply_cue_volume(self, state)
            return
        end
        if name == "save_config" or name == "cancel_config" then
            if name == "save_config" and state.volumeDraft ~= nil then
                game.volumeLevel = state.volumeDraft
            end
            if name == "save_config" and state.motionDraft ~= nil then
                game.backgroundMotion = state.motionDraft
            end
            state.volumeDraft = nil
            state.motionDraft = nil
            show_volume(self, state)
            apply_volume(self, state)
            apply_cue_volume(self, state)
            apply_motion(self, state)
            return
        end
        if name == "motion_on" or name == "motion_off" then
            state.motionDraft = name == "motion_on"
            apply_motion(self, state)
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
