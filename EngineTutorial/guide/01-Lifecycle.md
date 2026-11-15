# 第1章 起動から終了まで

教材を書き出した `Game.exe` を開き、目次で **第1章** を選びます。起動・通常フレーム・終了の小テスト3問と、最後の穴埋め1問があります。答えに応じてヒントや理由が表示されます。

## C++の穴埋め

小テストの最後で **穴埋め問題へ** を押します。`MainLoop()` の抜粋を読み、更新の失敗を呼び出し元に返す行を選びます。選んだ行がコードに入り、それぞれの行で何が起きるかが表示されます。

以下は、気になった処理の前後をソースで確認するときの案内です。

## 1. 起動

| 止める場所 | 見るもの |
| --- | --- |
| [Editorの `WinMain`](../../Editor/src/main.cpp) | `EngineConfig` のウィンドウ設定と、`CreateInitialScene` を関数として渡す箇所 |
| [`Engine::Run` と `Engine::Impl::Initialize`](../../Engine/src/core/Engine.cpp) | ログとCOMの準備 → シェーダーのコンパイル → ウィンドウ → D3D12 → 描画機能・入力・UI・音声 → `EngineContext` |
| [`SceneManager::RequestChange` と `ApplyPendingChange`](../../Engine/src/scene/SceneManager.cpp) | 予約されたシーンと現在のシーン。初期化が成功したシーンを現在のシーンへ移す順番 |
| [`EditorScene::Initialize`](../../Editor/src/scene/EditorScene.cpp) | Editorの描画やPlayの準備、設定の読み込み、前回のプロジェクトを復元する処理 |

`CreateInitialScene` に渡る [`EngineContext`](../../Engine/include/core/EngineContext.h) は、Engineが所有する共通機能への参照を持っています。ソース上でアプリ側に渡る参照を確認し、`Engine::Impl` のメンバー宣言と見比べます。シーンを作る関数、シーンを初期化する関数、それを保持するクラスを一つずつ確認してください。

## 2. 1フレーム

[`Engine::Impl::MainLoop`](../../Engine/src/core/Engine.cpp) の1周をステップ実行します。Windowsメッセージを処理してから入力を更新し、ウィンドウのサイズを確認します。最小化して幅・高さが無効になった場合は、メッセージを待ち、時間をリセットして次の周回に進みます。

通常は [`FrameTimer::Tick`](../../Engine/src/core/FrameTimer.cpp) の秒数を `deltaTime` に受け、切り替え予約を適用します。UIフレームを開始してシーンを更新し、そのあとで `graphics_.BeginFrame` → シーンの `Draw` → UI描画 → `graphics_.EndFrame` と進みます。[`EndFrame`](../../Engine/src/graphics/render/D3D12Graphics.cpp) では、記録した命令をGPUに送信し、`Present` で描画した画像を表示します。

## 3. 終了と後片付け

Editorのウィンドウを閉じます。[`Window.cpp` の `WM_CLOSE`](../../Engine/src/platform/windows/Window.cpp) で終了要求が記録されます。メインループが要求を受け取って `SceneManager::RequestClose` を呼び、Editorの [`RequestClose`](../../Editor/src/scene/EditorScene.cpp) が編集状態に応じて終了を許可するか判断します。未保存の変更を作って終了を取り消すと、メインループが続くことも確認できます。

終了が確定したら [`Engine::Impl::~Impl`](../../Engine/src/core/Engine.cpp) を追います。GPUの処理完了を待ち、`SceneManager::Finalize` から現在のシーンの `Finalize` を呼びます。そのあとで音声・モデル・UI・入力・描画・ウィンドウのリソースを解放します。COMのスコープを抜けたあと、成功または失敗のログが記録されます。

途中失敗の経路は、`Initialize` のエラー処理と `SceneManager::ApplyPendingChange` で確認します。後者は、次のシーンの初期化が失敗したとき、そのシーンの `Finalize` を呼び、現在のシーンを保持したまま呼び出し元へ `false` を返します。起動時・メインループ中の各呼び出し元が `false` をどう扱うかも見比べます。

## EditorとRuntimeの入口を比較する

[`Runtime` の `WinMain`](../../Runtime/src/main.cpp) は実行ファイルの場所から `game.json` を読み、ウィンドウ設定を `EngineConfig` へ渡します。[RuntimeScene](../../Runtime/src/scene/RuntimeScene.cpp) では、起動シーンを読み込んでシミュレーションを開始し、更新・描画・終了を同じ [`IScene`](../../Engine/include/scene/IScene.h) インターフェースに接続します。Editorでの **Stop** によるPlayの終了と、ウィンドウを閉じたときのEngine全体の終了を見比べてください。
