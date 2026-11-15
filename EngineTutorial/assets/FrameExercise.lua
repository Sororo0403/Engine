local answers = {
    success = {
        line = "return 0;",
        feedback = "Draw には進みませんが、0 を返すので Engine::Run は正常終了として扱います。",
    },
    skip = {
        line = "continue;",
        feedback = "このフレームの Draw を飛ばして次のフレームに進みます。失敗は Engine::Run に伝わりません。",
    },
    failure = {
        line = "return -1;",
        feedback = "正解。-1 を返すので、Draw に進まず、Engine::Run が異常終了として扱います。",
    },
}

return {
    message = function(self, _state, name)
        local answer = answers[name]
        if not answer then
            return
        end

        self:reference("code"):set_text(
            ("if (!sceneManager_.Update(deltaTime)) {\n    %s\n}"):format(answer.line))
        self:reference("feedback"):set_text(answer.feedback)
    end,
}
