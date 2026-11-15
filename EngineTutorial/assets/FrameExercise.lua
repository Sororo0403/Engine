local answers = {
    success = {
        line = "return 0;",
        feedback = "0 は正常終了を表します。更新に失敗した場合の終了コードを選びましょう。",
    },
    skip = {
        line = "continue;",
        feedback = "continue は次のフレームへ進みます。失敗を呼び出し元に返す行を選びましょう。",
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
