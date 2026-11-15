-- 実装の解説はシーン内の Lua Script の Initial State に保存する。
local rowsPerPage = 6
local entryRows = {entry1 = 1, entry2 = 2, entry3 = 3, entry4 = 4, entry5 = 5, entry6 = 6}

local function set_text(self, name, value)
    self:reference(name):set_text(value)
end

local function show_entry(self, entry)
    set_text(self, "path", entry.path)
    set_text(self, "topic", entry.topic)
    set_text(self, "explanation", entry.explanation)
    set_text(self, "follow", entry.follow)
end

local function show_page(self, state, page)
    local pageCount = math.ceil(#state.entries / rowsPerPage)
    if page < 1 or page > pageCount then
        return
    end
    state.page = page
    set_text(self, "page", ("%d / %d ページ"):format(page, pageCount))
    self:reference("previous"):set_interactable(page > 1)
    self:reference("next"):set_interactable(page < pageCount)

    local first = (page - 1) * rowsPerPage + 1
    for row = 1, rowsPerPage do
        local entry = state.entries[first + row - 1]
        self:reference("row" .. row):set_interactable(entry ~= nil)
        set_text(self, "label" .. row, entry and entry.label or "")
    end
    show_entry(self, state.entries[first])
end

return {
    start = function(self, state)
        show_page(self, state, 1)
    end,

    message = function(self, state, name)
        if name == "previous" then
            show_page(self, state, state.page - 1)
        elseif name == "next" then
            show_page(self, state, state.page + 1)
        else
            local row = entryRows[name]
            local entry = row and state.entries[(state.page - 1) * rowsPerPage + row]
            if entry then
                show_entry(self, entry)
            end
        end
    end,
}
