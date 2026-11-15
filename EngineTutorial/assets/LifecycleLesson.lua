local questions = {
    {
        source = "Editor/src/main.cpp  /  WinMain",
        code = [[EngineConfig config;
config.windowTitle = L"Editor";
config.windowWidth = 1600;
config.windowHeight = 900;

return Engine::Run(config, CreateInitialScene);]],
        prompt = "CreateInitialScene は、ここで何として渡されますか？",
        choices = {"作成済みのシーン", "シーンを作る関数", "保存済みシーンのパス"},
        correct = 2,
        hint = "ヒント  CreateInitialScene の後ろに () があるか見てください。",
        reason = "正解。Engineが共通機能を準備し、EngineContextを渡してこの関数を呼びます。",
    },
    {
        source = "Engine/src/core/Engine.cpp  /  MainLoop",
        code = [[if (!sceneManager_.Update(deltaTime)) {
    return -1;
}

// 音声・タイトル更新・BeginFrame を省略
sceneManager_.Draw();]],
        prompt = "このフレームの Update が false を返すと、次に何が起きますか？",
        choices = {"描画して次のフレームへ進む", "現在のシーンを再初期化する", "描画に進む前にループを抜ける"},
        correct = 3,
        hint = "ヒント  return -1 に到達すると、実行中の MainLoop() から戻ります。",
        reason = "正解。Draw() に到達する前に MainLoop() が戻り、Engine::Run が終了処理へ進みます。",
    },
    {
        source = "Engine/src/core/Engine.cpp  /  ~Impl",
        code = [[if (graphicsInitialized_) {
    if (!graphics_.WaitForGpu()) {
        // エラーログを省略
    }
}

// 参照している側から順に終了する
sceneManager_.Finalize();]],
        prompt = "描画機能を初期化済みの場合、シーンを解放する前に何をしますか？",
        choices = {"送信済みのGPU処理の完了を待つ", "次のシーンを読み込む", "先にウィンドウを破棄する"},
        correct = 1,
        hint = "ヒント  Finalize() より先に実行される WaitForGpu() の行を確認してください。",
        reason = "正解。シーンが持つ描画資源を解放する前に、GPUが使い終わるのを待ちます。",
    },
}

local answerNumbers = {answer1 = 1, answer2 = 2, answer3 = 3}

local function text(self, name, value)
    self:reference(name):set_text(value)
end

local function show_question(self, state, number)
    state.question = number
    state.answered = false

    local question = questions[number]
    text(self, "progress", ("問題  %d / %d"):format(number, #questions))
    text(self, "source", question.source)
    text(self, "code", question.code)
    text(self, "prompt", question.prompt)
    for index, choice in ipairs(question.choices) do
        text(self, "label" .. index, ("%d. %s"):format(index, choice))
    end
    text(self, "feedback", "コードを読んで、答えを選んでください。")
    text(self, "advanceLabel", number == #questions and "穴埋め問題へ" or "次の問題")
    self:reference("advance"):set_interactable(false)
end

return {
    start = function(self, state)
        show_question(self, state, 1)
    end,

    message = function(self, state, name)
        if name == "advance" then
            if state.answered then
                if state.question == #questions then
                    self:load_scene("practice")
                else
                    show_question(self, state, state.question + 1)
                end
            end
            return
        end

        local choice = answerNumbers[name]
        if not choice or state.answered then
            return
        end

        local question = questions[state.question]
        state.answered = choice == question.correct
        text(self, "feedback", state.answered and question.reason or question.hint)
        self:reference("advance"):set_interactable(state.answered)
    end,
}
