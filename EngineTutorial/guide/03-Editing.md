# 第3章 編集とUndo／Redo

目次で **第3章 編集とUndo／Redo** を開きます。選択とアクティブな物体、ギズモ操作の確定、Playと編集データ、保存地点、履歴カーソルを扱う5問です。

## 選択と編集の確定

HierarchyやSceneビューは、選択した物体をIDで扱います。[EditorSelection.cpp](../../Editor/src/selection/EditorSelection.cpp) の `GetIds()` は一覧を返し、`GetActiveId()` は末尾のIDを返します。単一選択では `SelectOnly()` が一覧を一件に絞ります。物体を削除したあとは `RemoveMissing()` で存在しないIDを取り除けます。

Inspectorやギズモから値を変えたとき、[EditorHistory.cpp](../../Editor/src/history/EditorHistory.cpp) の `CommitEdit()` が操作前と確定時の状態を比べます。複数選択のTransform編集では、変化した物体だけを `TransformCommand` に変換し、まとめて一つの履歴項目にします。

## 履歴と保存地点

`cursor_` は適用済みの履歴の直後です。Undoは `entries_[cursor_ - 1]` を、Redoは `entries_[cursor_]` を使います。コマンドをシーンに適用できたあとで、カーソルと現在の状態を更新します。Undoを重ねて保存時の状態IDに戻ると、編集中の変更がない場合は保存済みとして扱います。

[SceneDocument.cpp](../../Editor/src/scene/SceneDocument.cpp) は保存に成功してからファイル名と保存済みの状態を更新します。ディスクの内容が外部で変わっていた場合は、別名保存や読み直しを選べます。

## Playで使うシーン

[EditorPlaySession.cpp](../../Editor/src/gameplay/EditorPlaySession.cpp) は編集シーンを複製して試遊を開始します。通常のStopでは試遊中の落下やスクリプトによる変形を編集側に書き戻しません。試遊中の選択や変形を編集へ持ち帰る場合は、Gameパネルの専用操作から明示的に反映します。 [EditorScene.cpp](../../Editor/src/scene/EditorScene.cpp)
