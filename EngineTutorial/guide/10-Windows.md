# 第10章 Windowsと共通処理

目次で **第10章 Windowsと共通処理** を開きます。閉じる要求、実行ファイル基準のパス、UTF-8、アトミックな保存、終了通知の扱いを確認します。

## ウィンドウの要求と終了

[Window.cpp](../../Engine/src/platform/windows/Window.cpp) は `WM_SIZE` で寸法を保持し、`WM_CLOSE` では閉じる要求を記録します。[Engine.cpp](../../Engine/src/core/Engine.cpp) のメインループは要求を消費し、編集中なら終了を許可するかシーンへ問い合わせます。ウィンドウが破棄されると `WM_DESTROY` 側から終了通知を送ります。[WinApp.cpp](../../Engine/src/platform/windows/WinApp.cpp) が `WM_QUIT` を受け取るとメッセージ処理を終えます。

## パスと文字列

[PathUtils.cpp](../../Engine/src/platform/windows/PathUtils.cpp) の `ResolveFromExecutable` は相対パスの基準に実行ファイルのフォルダを使います。配布アプリの画像やシェーダーはこの経路で見つけられます。プロジェクトの相対参照をデータから解決する経路は第4章で扱います。

[StringConversion.cpp](../../Engine/src/platform/windows/StringConversion.cpp) はエンジン側のUTF-8とWindows APIに渡すワイド文字列を相互に変換します。必要な長さを調べてから変換し、不正なUTF-8には空のワイド文字列を返します。

## 保存と診断

[FileUtils.cpp](../../Engine/src/platform/windows/FileUtils.cpp) は一時ファイルへ書き、内容をフラッシュしたあとで保存先の名前へ変更します。失敗時は一時ファイルの片付けを試み、片付けができない場合は場所もエラーへ含めます。

[Logger.cpp](../../Engine/src/core/Logger.cpp) が処理の結果や失敗理由をログへ記録します。素材のパスを含む診断では、Editorから原因のファイルを確認できます。
