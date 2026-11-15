# 第7章 描画とGPU

目次で **第7章 描画とGPU** を開きます。シーンからの描画対象の抽出、カメラと影の範囲、頂点の座標、GPUの定数バッファ、光に背を向けた面を扱います。

**実装索引を開く** から、描画ワールド、GPUリソース、HLSLシェーダーに関わる44ファイルを確認できます。教材の目次からは **実装索引 → 第7章** を選びます。

## シーンから描画へ

[SceneRenderer.cpp](../../Engine/src/scene/SceneRenderer.cpp) は有効状態とコンポーネントを調べ、描画できる物体だけを描画ワールドに登録します。[ModelLoader.cpp](../../Engine/src/graphics/model/ModelLoader.cpp) はAssimpから形状や材質を読み、エンジンが扱うモデルへ変換します。[ModelManager.cpp](../../Engine/src/graphics/model/ModelManager.cpp) ではモデルを取得・共有し、物体ごとの姿勢や表示に使う値は描画側で管理します。

[RenderWorld.cpp](../../Engine/src/graphics/render/RenderWorld.cpp) は物体の境界をカメラの範囲と光源の範囲に対して別々に調べます。画面外の物体でも影には必要になり得ます。どちらにも不要なら、物体ごとの描画準備を飛ばします。

## C++からHLSLへ

[Mesh.VS.hlsl](../../Engine/resources/shaders/mesh/Mesh.VS.hlsl) は、頂点を `worldViewProjection` で投影用の座標に変換し、光源との計算に使うワールド位置も別に渡します。法線は座標の移動成分を含まない3×3部分で変換します。

[Mesh.PS.hlsl](../../Engine/resources/shaders/mesh/Mesh.PS.hlsl) はテクスチャを読み、環境光・光源・影を使ってピクセルの色を計算します。平行光の拡散項は、法線と光に向かうベクトルの内積を `saturate` で0〜1に収めます。このため、内積が負の面はその光から拡散光を受けません。

## 定数データとGPUの同期

[ConstantBufferPool.cpp](../../Engine/src/graphics/buffer/ConstantBufferPool.cpp) はアップロード用のページをスロットに分け、ページ先頭にスロット分のずれを足してGPUアドレスを作ります。使用しなくなった領域は再利用待ちへ移り、安全に再利用できる時点で空き領域へ戻します。

[D3D12Graphics.cpp](../../Engine/src/graphics/render/D3D12Graphics.cpp) はコマンドリストをGPUへ送り、スワップチェーンで表示し、フェンスで完了を確認します。Sceneビューの画像は [RenderTexture.cpp](../../Engine/src/graphics/render/RenderTexture.cpp) の描画先を経由してEditorに表示できます。
