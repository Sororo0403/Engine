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

local transferQuestion = {
    source = "Runtime/src/main.cpp  /  WinMain",
    code = [[if (!game.Load(executableDirectory / L"game.json", error)) {
    // ログとエラー表示を省略
    return -1;
}

// EngineConfig の設定を省略
const auto result = Engine::Run(config, CreateScene);]],
    prompt = "game.json が読めないとき、Engine::Run 内のブレークポイントには到達しますか？",
    choices = {"到達する。Run がエラーを処理する", "到達しない。WinMain が先に戻る", "シーンが読み込めた場合だけ到達する"},
    correct = 2,
    hint = "ヒント  呼び出し元の WinMain にある return -1 と Run の前後関係を見てください。",
    reason = "正解。Runtimeの入口で処理が戻ります。エラーは Engine::Run を呼ぶ前に記録されます。",
}

local sampleCount = 24
local targetMilliseconds = 50
local answerNumbers = {answer1 = 1, answer2 = 2, answer3 = 3}

local function text(self, name, value)
    self:reference(name):set_text(value)
end

local function show_question(self, state, number)
    state.question = number
    state.answered = false
    state.samples = nil
    local question = questions[number] or transferQuestion
    text(self, "progress", number <= #questions
        and ("小テスト  %d / %d"):format(number, #questions)
        or "応用問題  Runtime の起動")
    text(self, "source", question.source)
    text(self, "code", question.code)
    text(self, "prompt", question.prompt)
    for index, choice in ipairs(question.choices) do
        self:reference("answer" .. index):set_active(true)
        text(self, "label" .. index, ("%d. %s"):format(index, choice))
    end
    text(self, "feedback", "コードを読んで、答えを一つ選んでください。")
    text(self, "advanceLabel", number >= #questions and "C++実習へ" or "次の問題")
    self:reference("advance"):set_interactable(false)
end

local function show_experiment(self, state)
    state.question = nil
    state.samples = nil
    text(self, "progress", "C++実習  更新に渡す時間を変える")
    text(self, "source", "Engine/src/core/Engine.cpp  /  MainLoop")
    text(self, "code", [[// 変更前
if (!sceneManager_.Update(deltaTime)) {
    return -1;
}

// 実験中だけ、1回の更新を 0.05 秒にする
if (!sceneManager_.Update(0.05f)) {
    return -1;
}]])
    text(self, "prompt", "まず計測して値を控えます。\n\nEditorを閉じ、右のように Update の引数を書き換えてビルドします。\n\n同じ実習を開き、もう一度計測してください。")
    for index = 1, #questions[1].choices do
        self:reference("answer" .. index):set_active(false)
    end
    text(self, "feedback", "Play中に計測します。実習後は引数を deltaTime に戻してください。")
    text(self, "advanceLabel", "24回計測する")
    self:reference("advance"):set_interactable(true)
end

return {
    start = function(self, state)
        show_question(self, state, 1)
    end,

    message = function(self, state, name)
        if name == "quiz" then
            show_question(self, state, 1)
        elseif name == "transfer" then
            show_question(self, state, #questions + 1)
        elseif name == "experiment" then
            show_experiment(self, state)
        elseif name == "advance" then
            if not state.question then
                state.samples = 0
                state.totalSeconds = 0
                state.nearTarget = 0
                text(self, "feedback", "更新時間を計測中…")
                self:reference("advance"):set_interactable(false)
            elseif state.answered then
                if state.question >= #questions then
                    show_experiment(self, state)
                else
                    show_question(self, state, state.question + 1)
                end
            end
        elseif state.question and not state.answered then
            local choice = answerNumbers[name]
            if not choice then
                return
            end
            local question = questions[state.question] or transferQuestion
            state.answered = choice == question.correct
            text(self, "feedback", state.answered and question.reason or question.hint)
            self:reference("advance"):set_interactable(state.answered)
        end
    end,

    update = function(self, state, seconds)
        if state.samples == nil or seconds <= 0 then
            return
        end

        state.samples = state.samples + 1
        state.totalSeconds = state.totalSeconds + seconds
        if math.abs(seconds * 1000 - targetMilliseconds) < 0.25 then
            state.nearTarget = state.nearTarget + 1
        end
        if state.samples < sampleCount then
            return
        end

        local mean = state.totalSeconds * 1000 / sampleCount
        local fixedStep = state.nearTarget == sampleCount
        text(self, "feedback", ("平均 %.2f ms  /  50 ms付近は %d / %d 回\n%s"):format(
            mean, state.nearTarget, sampleCount,
            fixedStep and "更新に渡る値は毎回ほぼ50 msです。これだけでは描画が20 FPSとはいえません。"
                or "引数の変更後は、全24回がほぼ50 msになることを確認します。"))
        state.samples = nil
        self:reference("advance"):set_interactable(true)
    end,
}
