# 第2章 シーンとコンポーネント

目次で **第2章 シーンとコンポーネント** を開きます。同じ名前の物体、描画に必要なコンポーネント、親子の変形と有効状態、親子関係の循環を扱う5問です。

## 物体の識別

[Scene.cpp](../../Engine/src/scene/Scene.cpp) では `CreateObject` が新しいIDで物体を格納し、`FindObject` はIDで検索します。同名の物体を別々に扱えます。名前やタグはその物体が持つデータです。削除されたIDを渡すと `FindObject` は `nullptr` を返すので、呼び出し元が存在を確認します。

## コンポーネントの組み合わせ

[SceneObject.h](../../Engine/include/scene/SceneObject.h) では、全物体に `Transform` があり、それ以外の機能を多くの場合 `std::optional` で持ちます。`Mesh` はモデルの参照、`MeshRenderer` は表示や材質の設定、`Collider` は衝突形状です。`HasMesh()` は形状が割り当て済みかを調べ、`IsRenderable()` はさらに `MeshRenderer` が存在し、表示が有効かを調べます。[SceneRenderer.cpp](../../Engine/src/scene/SceneRenderer.cpp) では描画対象を選ぶときに、物体の実行時の有効状態も確認します。

## 親子関係

[SceneHierarchy.cpp](../../Engine/src/scene/SceneHierarchy.cpp) は親を先に評価し、各物体のローカル変形を親のワールド変形と合成します。単純な移動だけの例なら、親のXが10で子のローカルXが2のとき、子のワールドXは12になります。回転や拡大率がある場合も関数 `Compose` が行列で合成します。

[SceneActivity.cpp](../../Engine/src/scene/SceneActivity.cpp) では、物体自身の `active` と親から継承した状態から有効性を計算します。親が無効になると子も実行上は無効になり、子自身の設定は保持されます。

`SetParent` では、新しい親から祖先をたどって移動対象自身に出会うと失敗を返します。自分の子を新しい親にすると循環するためです。親を変更できる場合、`keepWorld` を指定すると、変更前と同じワールド変形になるよう新しいローカル変形を求めます。
