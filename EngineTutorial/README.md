# EngineTutorial

自分で C++ を書き換え、実行結果を見ながら Engine の使い方を学ぶサンプルです。

1. Visual Studio で `Engine.slnx` を開きます。
2. ソリューション エクスプローラーで **EngineTutorial** を右クリックし、**スタートアップ プロジェクトに設定**します。
3. **Release | x64** または **Debug | x64** で実行します。変更を試すときはウィンドウを閉じてからビルドします。

## 第1章 ウィンドウとメインループ

[main.cpp](src/main.cpp) のタイトルとサイズを書き換えます。画面に表示される更新回数と描画回数を比べてから、終了後に実行ファイルの隣の `logs/Engine.log` を開きます。[TutorialScene.cpp](src/TutorialScene.cpp) にある4つの関数がどの順番で呼ばれたか確認できます。

## 第2章 箱とTransform

[BoxLesson.cpp](src/BoxLesson.cpp) の `CreateLessonBox()` で、箱の `translation.x` と `scale.y` を変更します。ビルドして **2 箱の表示** を開き、変更前と比べます。**親のY回転** を動かすと、`parentId` を設定した黄色い箱が、親の周りを動きます。

`Scene::CreateObject()` から `SceneRenderer::Update()` をたどると、C++で作成した物体が描画に反映される流れを確認できます。
