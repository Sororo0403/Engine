# Engine

- `Engine/` にエンジン本体を置きます。
- `Editor/` に制作ツールのソースと、ツール自体が使うリソースを置きます。
- `Runtime/` にエクスポートしたゲームを実行するランタイムを置きます。

## ゲームプロジェクトを開く

1. `Engine.slnx` を Visual Studio で開き、`Editor` を起動します。
2. Editor の `Open Project...` から、ゲームの `project.json` またはそのファイルが入っているフォルダを選びます。新しく作る場合は `New Project...` で、このリポジトリとは別の場所を選びます。
3. プロジェクトを移すときは、`project.json`、`scenes/`、`assets/` を含むフォルダ全体を移し、移動先から開き直します。

Stage Flow で使う `stage-flow.json` や `decoration-theme.json`、区間 Prefab、生成したシーンは、そのステージを作るゲームプロジェクトに置きます。
