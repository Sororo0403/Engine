local menu = {
    { button = "start", shadow = "start_shadow", y = 450, delay = 0.55 },
    { button = "config", shadow = "config_shadow", y = 518, delay = 0.65 },
    { button = "quit", shadow = "quit_shadow", y = 586, delay = 0.75 },
}

local function smooth(t)
    t = math.max(0, math.min(1, t))
    return t * t * (3 - 2 * t)
end

local function place_menu(self, time)
    for _, item in ipairs(menu) do
        local t = math.max(0, math.min(1, (time - item.delay) / 0.57))
        local remaining = t - 1
        local eased = 1 + 2.15 * remaining ^ 3 + 1.15 * remaining ^ 2
        local x = -380 + 468 * eased
        local button = self:reference(item.button)
        button:set_ui_position(x, item.y)
        button:set_interactable(t >= 0.8)
        self:reference(item.shadow):set_ui_position(x + 2, item.y + 2)
    end

    self:reference("logo"):set_image_color(1, 1, 1, smooth(time / 0.55))
end

local function cover(self, opacity)
    self:reference("cover"):set_image_color(0, 0, 0, opacity)
end

local function apply_palette(self, mode)
    for _, tag in ipairs({ "title_dark", "title_start_source", "title_quit_source" }) do
        for _, object in ipairs(scene.find_all_by_tag(tag)) do
            object:set_mesh_tint(0, 0, 0)
        end
    end

    local sources = mode == "start" and "title_start_source" or "title_quit_source"
    for _, object in ipairs(scene.find_all_by_tag(sources)) do
        object:set_mesh_visible(false)
    end

    self:reference("morning_light"):set_active(false)
    self:reference("palette_lights"):set_active(true)
    self:reference(mode == "start" and "start_palette" or "quit_palette"):set_active(true)
end

return {
    start = function(self, state)
        state.mode = "intro"
        state.seconds = 0
        place_menu(self, 0)
    end,

    message = function(self, state, name)
        if name ~= "start" and name ~= "quit" then
            return
        end

        state.mode = name
        state.seconds = 0
        self:reference("logo"):set_active(false)
        for _, item in ipairs(menu) do
            local button = self:reference(item.button)
            button:set_text_color(1, 1, 1, 0)
            button:set_interactable(false)
            self:reference(item.shadow):set_active(false)
        end

        local confirm = self:reference("confirm")
        confirm:set_image_color(1, 1, 1, 0)
        confirm:set_text_color(1, 1, 1, 0)
        confirm:set_interactable(false)
        self:reference("quit_panel"):set_active(false)
        self:reference("quit_heading"):set_active(false)
        self:reference("quit_caption"):set_active(false)
        self:reference("quit_cancel"):set_active(false)
    end,

    update = function(self, state, seconds)
        if state.mode == "idle" then
            return
        end

        state.seconds = state.seconds + seconds
        local t = state.seconds
        if state.mode == "intro" then
            place_menu(self, t)
            if t >= 1.32 then
                state.mode = "idle"
            end
            return
        end

        if t < 0.30 then
            cover(self, smooth(t / 0.30))
        elseif t < 0.75 then
            if not state.palette_applied then
                apply_palette(self, state.mode)
                state.palette_applied = true
            end
            cover(self, 1 - smooth((t - 0.30) / 0.45))
        else
            if not state.palette_applied then
                apply_palette(self, state.mode)
                state.palette_applied = true
            end
            cover(self, smooth((t - 1.45) / 0.65))
        end
    end,
}
