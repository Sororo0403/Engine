# GameplayTests

DirectXの描画やウィンドウを起動せず、実際のEngineライブラリでゲーム制作に必要な処理を検証する。

Visual Studioの開発者用ターミナルから実行する。

```powershell
msbuild Engine.slnx /m:1 /p:Configuration=Debug /p:Platform=x64
./build/tests/Debug/GameplayTests.exe
msbuild Engine.slnx /m:1 /p:Configuration=Release /p:Platform=x64
./build/tests/Release/GameplayTests.exe
```

現在の検証範囲は、衝突形状のワールド座標変換、旧シーン形式との互換性、衝突設定の保存・読み込み、無効な設定での状態保持、物体のUndo／Redo。
