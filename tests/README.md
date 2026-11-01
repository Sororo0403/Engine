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

衝突・主人公制御の検証には、薄い壁を横切る高速移動、壁沿いの移動、床・天井・角への接触、フレームレート間の一致、斜め移動、ジャンプと着地、空中ジャンプの禁止、崖からの落下、最大落下速度、位置リセットを含む。

Editorの衝突設定については、モデル頂点からの形状合わせ、コンポーネント追加・連続編集・削除のUndo／Redo、未保存状態、複製・削除後の形状保持も検証する。
