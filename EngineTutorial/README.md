# EngineTutorial

実際のC++とHLSLを読み、小テストとコードの穴埋めでエンジンの動きを学ぶ教材です。このフォルダは既存Editorで開けるプロジェクトです。

1. Editorの **File → Open Project…** からこのフォルダの `project.json` を開きます。
2. **File → Export Game…** で新しいフォルダへ出力します。書き出しには、対応するRelease x64のRuntimeが必要です。
3. 出力した `Game.exe` を起動し、目次から学びたい章を開きます。

候補を選ぶと、空欄に入ったコードと、その行を実行した場合の動きが表示されます。

目次の **実装索引** から、章ごとのファイルや外部ライブラリの接続箇所を調べられます。ファイル名から探す場合は、対応表の「アプリ内の行き先」を確認します。

- [全章の構成とファイルごとの対応表](guide/README.md)
- [第1章 起動から終了まで](guide/01-Lifecycle.md)
- [第2章 シーンとコンポーネント](guide/02-SceneObjects.md)
- [第3章 編集とUndo／Redo](guide/03-Editing.md)
- [第4章 保存・素材・Prefab](guide/04-Assets.md)
- [第5章 入力とアクション](guide/05-Input.md)
- [第6章 シミュレーションと物理](guide/06-Simulation.md)
- [第7章 描画とGPU](guide/07-Rendering.md)
- [第8章 Lua・アニメーション・粒子](guide/08-ScriptsAndMotion.md)
- [第9章 音と画面UI](guide/09-AudioAndUI.md)
- [第10章 Windowsと共通処理](guide/10-Windows.md)
- [第11章 ビルドとRuntime配布](guide/11-Distribution.md)
