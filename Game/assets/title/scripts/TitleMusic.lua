local function volume_level(state)
    return state.volumeDraft or game.volumeLevel or 10
end

local cue_names = {"confirmSound", "backSound", "adjustSound", "selectSound"}

local config_rows = {
    config_focus_volume = 1,
    config_focus_motion = 2,
    config_focus_save = 3,
}

local home_buttons = {
    title_focus_start = "startButton",
    title_focus_config = "configButton",
    title_focus_quit = "quitButton",
}

local function mark_home_button(self, name)
    local marker = self:reference("homeMarker")
    if marker then
        marker:set_parent(self:reference(name), false)
    end
end

local function mark_config_row(self, state, row)
    local marker = self:reference("configMarker")
    if marker then
        marker:set_ui_position(state.configMarkerX, state.configMarkerRows[row])
    end
end

local function apply_cue_volume(self, state)
    for i, name in ipairs(cue_names) do
        local cue = self:reference(name)
        if cue then
            cue:set_audio_volume(state.cueVolumes[i] * volume_level(state) / 10)
        end
    end
end

local function show_volume(self, state)
    self:reference("volumeLabel"):set_text(volume_level(state) * 10 .. "%")
end

local function set_menu_interactable(self, enabled)
    for _, name in ipairs({"startButton", "configButton", "quitButton"}) do
        local button = self:reference(name)
        if button then
            button:set_interactable(enabled)
        end
    end
end

local function apply_motion(self, state)
    local enabled = state.motionDraft
    if enabled == nil then
        enabled = game.backgroundMotion ~= false
    end
    local camera = self:reference("idleCamera")
    if camera then
        camera:set_animation_clip_paused(not enabled)
        local logo = self:reference("idleLogo")
        if logo then
            logo:set_animation_clip_paused(not enabled)
        end
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
            local cue = self:reference(name)
            if cue then
                state.cueVolumes[i] = cue:get_audio_volume()
            end
        end
        apply_cue_volume(self, state)
        apply_motion(self, state)
        local returnTimeline = self:reference("returnTimeline")
        if returnTimeline then
            local returning = game.returningToTitle == true
            game.returningToTitle = nil
            returnTimeline:set_active(returning)
            self:reference("returnCover"):set_active(returning)
            if returning then
                self:reference("introTimeline"):set_animation_clip_paused(true)
                set_menu_interactable(self, false)
            end
        end
    end,

    message = function(self, state, name)
        if state.departing then
            return
        end
        if name == "title_return_complete" then
            set_menu_interactable(self, true)
            return
        end
        if home_buttons[name] then
            mark_home_button(self, home_buttons[name])
            return
        end
        if config_rows[name] then
            mark_config_row(self, state, config_rows[name])
            return
        end
        if name == "volume_down" or name == "volume_up" or name == "volume_cycle" then
            mark_config_row(self, state, 1)
            local level = volume_level(state)
            if name == "volume_cycle" then
                state.volumeDraft = (level + 1) % 11
            else
                local step = name == "volume_down" and -1 or 1
                state.volumeDraft = math.min(10, math.max(0, level + step))
            end
            show_volume(self, state)
            apply_volume(self, state)
            apply_cue_volume(self, state)
            return
        end
        if name == "save_config" or name == "cancel_config" then
            mark_home_button(self, "startButton")
            if name == "save_config" and state.volumeDraft ~= nil then
                game.volumeLevel = state.volumeDraft
            end
            if name == "save_config" and state.motionDraft ~= nil then
                game.backgroundMotion = state.motionDraft
            end
            state.volumeDraft = nil
            state.motionDraft = nil
            mark_config_row(self, state, 1)
            show_volume(self, state)
            apply_volume(self, state)
            apply_cue_volume(self, state)
            apply_motion(self, state)
            return
        end
        if name == "motion_on" or name == "motion_off" then
            mark_config_row(self, state, 2)
            state.motionDraft = name == "motion_on"
            apply_motion(self, state)
            return
        end
        if name ~= "start_departure" and name ~= "quit_departure" then
            return
        end
        if name == "start_departure" then
            set_menu_interactable(self, false)
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
