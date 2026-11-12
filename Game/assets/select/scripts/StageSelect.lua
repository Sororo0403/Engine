local details = {
    ring = "体当たりで敵を押し出す。",
    outlet = "スタンプを集め、時計台へ。",
    wind = "風に乗って、雲の上へ。",
}

return {
    start = function(self, state)
        state.selected = "ring"
        local preview = self:reference("ring")
        local x, _, z = preview:get_local_position()
        preview:set_local_position(x, state.standHeight + state.selectedLift, z)
        self:reference("details"):set_text(details.ring)
    end,

    message = function(self, state, name)
        if name == "return_to_title" then
            game.returningToTitle = true
            self:load_scene("title")
            return
        end

        if not details[name] or state.selected == name then
            return
        end
        state.selected = name
        for key in pairs(details) do
            local selected = key == name
            local preview = self:reference(key)
            local x, _, z = preview:get_local_position()
            preview:set_local_position(x, state.standHeight + (selected and state.selectedLift or 0), z)
            self:reference("card_" .. key):set_image_color(1, selected and 0.92 or 1,
                selected and 0.73 or 1, 1)
        end
        self:reference("details"):set_text(details[name])
    end
}
