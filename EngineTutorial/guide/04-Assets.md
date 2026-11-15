# 第4章 保存・素材・Prefab

目次で **第4章 保存・素材・Prefab** を開きます。プロジェクトとシーンの相対パス、保存失敗時の状態、Assetsで移動した素材の参照、Prefabの更新を扱う5問です。

## プロジェクトとシーン

[ProjectDocument.cpp](../../Editor/src/scene/ProjectDocument.cpp) は `project.json` の形式とバージョンを検証し、`assets` と `scenes` をプロジェクトの下で開きます。`startupScene` は `scenes` 内への相対パスです。空なら起動シーンは未設定です。

[SceneSerializer.cpp](../../Engine/src/scene/SceneSerializer.cpp) はプロジェクト内の素材を `@project/assets/…` の形で保存します。シーンファイルの位置からプロジェクトルートを求め、相対パスを結合してから、その素材がルート内にあるかを確認します。読み込んだ物体は候補のシーンに組み立て、親子関係やコンポーネントを検証したあとで編集シーンに適用します。

[SceneDocument.cpp](../../Editor/src/scene/SceneDocument.cpp) は保存に成功してから、保存先、最後に保存した内容、Undo履歴の保存地点を更新します。保存そのものは [FileUtils.cpp](../../Engine/src/platform/windows/FileUtils.cpp) の `WriteTextAtomically` へ渡します。

## アセットを移す

[AssetUsage.cpp](../../Editor/src/assets/AssetUsage.cpp) は、旧パスと新パスの対応を用いて、編集中のデータ・保存済みシーン・Prefabの参照を置き換えます。対応表にないパスは保持します。[SceneAssetReferences.cpp](../../Engine/src/scene/SceneAssetReferences.cpp) が、どのコンポーネントにファイル参照があるかをまとめて扱います。Prefabインスタンスでは、配置時の基準データに含まれる参照も更新します。

## Prefabの更新

[PrefabSerializer.cpp](../../Engine/src/scene/PrefabSerializer.cpp) はPrefabの階層と参照を保存・検証します。配置すると、Prefab内の元IDからシーン内で使うIDへの対応ができます。[PrefabInstances.cpp](../../Editor/src/assets/PrefabInstances.cpp) の `Mapped` は、この対応で親子関係や物体参照を付け替えます。

同ファイルの `Merge` は配置時の基準、現在の配置、最新のアセットを比較します。たとえば配置後にその箱の名前を変え、色は変更しなかった場合、アセット更新では個別の名前を保持しながら最新の色を取り込めます。配列やコンポーネント自体の追加・削除は一単位として比較します。
