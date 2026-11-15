# 第11章 ビルドとRuntime配布

目次で **第11章 ビルドとRuntime配布** を開きます。Editorで作ったデータが配布フォルダに入り、書き出した実行ファイルで読み込まれるまでをたどります。

## Release Runtimeと出力

[Runtime.vcxproj](../../Runtime/Runtime.vcxproj) はMSVCのバージョンと構成で出力先を分け、Releaseのビルド後にCRTのDLLを配置します。[Runtimeのmain.cpp](../../Runtime/src/main.cpp) の `--write-build-info` 処理は構成・アーキテクチャ・データ形式のバージョンを出力します。[GameExporter.cpp](../../Editor/src/export/GameExporter.cpp) はこれを使って、Release x64とEditorのデータ形式がそろっているかを確認します。

`GameExporter::Check` と `GameExporter::Export` は同じ出力計画を作ります。計画には `Runtime.exe` から `Game.exe` へのコピー、依存DLL、必要な素材、シーン、ライセンスが含まれます。書き出し時には素材やシーンへの参照を配布先用に変換し、起動設定を `game.json` に保存します。

## 起動と試遊

配布後は実行ファイルのフォルダから `game.json` を読み込みます。読み込みに成功するとタイトルや画面サイズを `EngineConfig` へ渡し、`Engine::Run` を開始します。設定を読み込めないと起動前に失敗し、ログやダイアログで原因を知らせます。

[RuntimeScene.cpp](../../Runtime/src/scene/RuntimeScene.cpp) はシーンを読み込んでシミュレーションを始めます。通常起動とEditorからの試遊では保存先が分かれています。セーブを使わない試遊では保存先を設定せず、読み書きを行いません。既存のセーブを読み込めない場合も、初期値による上書きを避けるため、その起動中の保存を無効にします。
