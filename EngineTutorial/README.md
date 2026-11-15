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

## 第3章 入力と移動時間

**3 入力と移動** を開き、WASDで箱を動かします。**1秒間の移動距離を比較** では、同じ移動処理にDキーを渡し、1秒を30・60・120回に分けて動かします。初期コードの結果は順に約1.50、3.00、6.00です。

[MovementLesson.cpp](src/MovementLesson.cpp) の `StepDistance()` で、`return speed / 60.f;` を `return speed * seconds;` に変えてビルドします。すべての更新回数で約3.00になることを確かめます。最後に `speed` を変え、計測結果とWASDでの動きの両方に同じ変更が効くことを確認します。

## 第4章 カメラ

**4 カメラ** でWASDを押し、固定と追従を切り替えます。[CameraLesson.cpp](src/CameraLesson.cpp) の `MakeFollowCamera()` で、`offset.y` を `8.f` に変更してから再ビルドし、見下ろす角度を比べます。次は `offset.z` を `-12.f` に変え、箱までの距離を比べてください。画面内の位置・注視点の数値から、箱を動かしたときにカメラがどう追従しているか確認できます。
