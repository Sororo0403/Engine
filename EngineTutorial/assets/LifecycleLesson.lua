local questions = {
    {
        source = "Editor/src/main.cpp  /  WinMain",
        code = [[EngineConfig config;
config.windowTitle = L"Editor";
config.windowWidth = 1600;
config.windowHeight = 900;

return Engine::Run(config, CreateInitialScene);]],
        prompt = "CreateInitialScene は、いつ呼ばれて何を受け取りますか？",
        choices = {"起動直後に EngineConfig を受け取る", "準備後に EngineContext を受け取る", "毎フレーム経過時間を受け取る"},
        correct = 2,
        feedback = {
            "ヒント  EngineConfig は Engine::Run に渡す起動設定です。シーンを作る関数は、共通機能の準備後に EngineContext を受け取ります。",
            "正解。Engineが共通機能を準備し、EngineContextを渡してこの関数を呼びます。",
            "ヒント  経過時間を受け取るのは、作成済みのシーンを毎フレーム更新する Update です。CreateInitialScene は最初のシーンを作るために呼ばれます。",
        },
    },
    {
        source = "Engine/src/core/Engine.cpp  /  MainLoop",
        code = [[if (!sceneManager_.Update(deltaTime)) {
    // 失敗時の処理を省略
}

// 音声・タイトル更新・BeginFrame を省略
sceneManager_.Draw();

// UI描画を省略
if (!graphics_.EndFrame()) {
    // 失敗時の処理を省略
}]],
        prompt = "通常の1フレームでは、状態の更新・描画・画面への表示はどの順ですか？",
        choices = {"描画 → 状態の更新 → 表示", "状態の更新 → 表示 → 描画", "状態の更新 → 描画 → 表示"},
        correct = 3,
        feedback = {
            "ヒント  Update が Draw より前に実行されるので、このフレームで更新した状態が Draw に使われます。",
            "ヒント  Draw は描画命令を記録します。EndFrame はそのあとで命令を送り、Present で描画結果を表示します。",
            "正解。Update → Draw → EndFrame の順です。EndFrame は描画命令をGPUへ送り、Present で表示します。",
        },
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
        prompt = "シーンを解放する前に WaitForGpu() を呼ぶのはなぜですか？",
        choices = {"GPUが描画資源をまだ使っているため", "次のシーンを読み込むため", "現在のシーンを保存するため"},
        correct = 1,
        feedback = {
            "正解。シーンが持つ描画資源を解放する前に、GPUが使い終わるのを待ちます。",
            "ヒント  次のシーンの初期化は切り替え時に行います。この終了処理の WaitForGpu は、送信済みの描画命令の完了を待ちます。",
            "ヒント  保存の可否は、閉じる要求を受けたシーンが判断します。ここでは後片付けの前にGPUの描画が終わるのを待ちます。",
        },
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
    text(self, "progress", ("問題  %d / %d"):format(number, #questions + 1))
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
        text(self, "feedback", question.feedback[choice])
        self:reference("advance"):set_interactable(state.answered)
    end,
}
