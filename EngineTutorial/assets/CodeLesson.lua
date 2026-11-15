-- 章ごとの問題はシーン内の Lua Script の Initial State に保存する。
local choices = {answer1 = 1, answer2 = 2, answer3 = 3}

local function text(self, name, value)
    self:reference(name):set_text(value)
end

local function set_code(self, question, choice)
    if question.blank then
        local line = choice and question.options[choice] or "________"
        text(self, "code", ("%s%s%s"):format(question.code, line, question.after))
    else
        text(self, "code", question.code)
    end
end

local function show_question(self, state, index)
    state.index = index
    state.answered = false
    local question = state.questions[index]

    text(self, "progress", ("問題  %d / %d"):format(index, #state.questions))
    text(self, "source", question.source)
    set_code(self, question)
    text(self, "prompt", question.prompt)
    for i, option in ipairs(question.options) do
        text(self, "label" .. i, ("%d. %s"):format(i, option))
    end
    text(self, "feedback", question.blank and "空欄に入るコードを選んでください。" or "コードを読んで、答えを選んでください。")
    text(self, "advanceLabel", index == #state.questions and "もう一度" or "次の問題")
    self:reference("advance"):set_interactable(false)
end

return {
    start = function(self, state)
        show_question(self, state, 1)
    end,

    message = function(self, state, name)
        if name == "advance" then
            if state.answered then
                show_question(self, state, state.index % #state.questions + 1)
            end
            return
        end

        local choice = choices[name]
        if not choice or state.answered then
            return
        end
        local question = state.questions[state.index]
        state.answered = choice == question.correct
        set_code(self, question, choice)
        text(self, "feedback", question.feedback[choice])
        self:reference("advance"):set_interactable(state.answered)
    end,
}
