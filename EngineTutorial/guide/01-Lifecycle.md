# 第1章 起動から終了まで

教材の目次シーンで **Play → 第1章 起動から終了まで** を開きます。右側に出る実装を読み、何が起きるかと、その根拠になる行を考えてから答えを選びます。答え合わせのあとにVisual Studioで該当箇所を開き、前後の処理を確認してください。

**C++実習** は、コード変更前の値を測り、変更後の結果を予想してから、実際に書き換えて再計測します。最後に **応用問題** で、Runtimeの入口でも同じ考え方が使えるか確かめます。時間を空けて「最初のシーンは誰が作るか」「更新失敗後に描画するか」「GPUを待つのはなぜか」を、画面を開く前に思い出してみてください。

起動のコードで止めるときは、Editorを閉じてから Visual Studioで **Editor / Debug / x64** を選び、ブレークポイントを置いて **F5** で起動します。

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

通常は [`FrameTimer::Tick`](../../Engine/src/core/FrameTimer.cpp) の秒数を `deltaTime` に受け、切り替え予約を適用します。UIフレームを開始してシーンを更新し、そのあとで `graphics_.BeginFrame` → シーンの `Draw` → UI描画 → `graphics_.EndFrame` と進みます。[`EndFrame`](../../Engine/src/graphics/render/D3D12Graphics.cpp) では、記録した命令をGPUに送信し、`Present` で描画した画像を表示します。デバッガで止めると計測値にも待ち時間が入るため、時間の値を比較するときは連続実行してください。

### C++実習

1. 第1章の **C++実習 → 24回計測する** を押し、表示された平均と回数を控えます。単位はミリ秒です。
2. Editorを閉じ、[`Engine.cpp`](../../Engine/src/core/Engine.cpp) の `MainLoop()` で、`sceneManager_.Update(deltaTime)` の引数を `0.05f` に変えます。描画の速さと更新に渡る値のどちらが変わるか、再実行前に予想します。
3. Visual StudioでEditorをビルドし、同じ第1章の **C++実習** で再計測します。更新処理に渡った24回分が、すべて約50 msになれば変更を確認できています。ここで測るのは `Update` の引数です。GPUへの命令送信と画面の表示は、同じループ内の後続処理です。
4. 実習後は引数を `deltaTime` に戻し、Editorを再ビルドします。

### 追加実習 フレーム順をログで確かめる

[`Engine.cpp`](../../Engine/src/core/Engine.cpp) の `MainLoop()` で、`while` の前にフラグを追加します。

```cpp
bool traceFirstFrame = true;
```

次の3か所にコードを入れ、最初に正常完了したフレームの順番を記録します。

1. `sceneManager_.Update(deltaTime)` の直前

   ```cpp
   if (traceFirstFrame) {
       Logger::Info(std::format("教材 1: Update dt={:.4f} 秒", deltaTime));
   }
   ```

2. `sceneManager_.Draw()` の直前

   ```cpp
   if (traceFirstFrame) {
       Logger::Info("教材 2: Draw");
   }
   ```

3. `graphics_.EndFrame()` が成功したことを確認する `if` ブロックの直後

   ```cpp
   if (traceFirstFrame) {
       Logger::Info("教材 3: EndFrame 完了");
       traceFirstFrame = false;
   }
   ```

Editorを閉じてビルドし、起動して終了します。実行ファイルの隣の `logs/Engine.log` で、今回の「メインループを開始しました。」に続いて教材1、2、3が一度ずつ出ることを確認します。

## 3. 終了と後片付け

Editorのウィンドウを閉じます。[`Window.cpp` の `WM_CLOSE`](../../Engine/src/platform/windows/Window.cpp) で終了要求が記録されます。メインループが要求を受け取って `SceneManager::RequestClose` を呼び、Editorの [`RequestClose`](../../Editor/src/scene/EditorScene.cpp) が編集状態に応じて終了を許可するか判断します。未保存の変更を作って終了を取り消すと、メインループが続くことも確認できます。

終了が確定したら [`Engine::Impl::~Impl`](../../Engine/src/core/Engine.cpp) を追います。GPUの処理完了を待ち、`SceneManager::Finalize` から現在のシーンの `Finalize` を呼びます。そのあとで音声・モデル・UI・入力・描画・ウィンドウのリソースを解放します。COMのスコープを抜けたあと、成功または失敗のログが記録されます。

途中失敗の経路は、`Initialize` のエラー処理と `SceneManager::ApplyPendingChange` で確認します。後者は、次のシーンの初期化が失敗したとき、そのシーンの `Finalize` を呼び、現在のシーンを保持したまま呼び出し元へ `false` を返します。起動時・メインループ中の各呼び出し元が `false` をどう扱うかも見比べます。

シェーダー読み込みの失敗を再現するには、Editorの出力フォルダを別の場所にコピーし、コピー側の `resources/engine/shaders/mesh/Mesh.VS.hlsl` を一時的に改名します。コピーしたEditorを起動すると、ログにファイルの読み込み失敗と終了コード `-1` が記録されます。メインループへ入る前に終了する理由を `Initialize` と `Engine::Run` で追ってください。

## EditorとRuntimeの入口を比較する

[`Runtime` の `WinMain`](../../Runtime/src/main.cpp) は実行ファイルの場所から `game.json` を読み、ウィンドウ設定を `EngineConfig` へ渡します。[RuntimeScene](../../Runtime/src/scene/RuntimeScene.cpp) では、起動シーンを読み込んでシミュレーションを開始し、更新・描画・終了を同じ [`IScene`](../../Engine/include/scene/IScene.h) インターフェースに接続します。Editorでの **Stop** によるPlayの終了と、ウィンドウを閉じたときのEngine全体の終了を見比べてください。
