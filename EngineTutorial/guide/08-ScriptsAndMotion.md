# 第8章 Lua・アニメーション・粒子

目次で **第8章 Lua・アニメーション・粒子** を開きます。Luaのモジュールと実行制限、キー補間、骨と頂点、粒子の寿命を扱います。

## Luaを呼ぶ境界

[LuaScript.cpp](../../Engine/src/scripting/LuaScript.cpp) はスクリプトのテキストから実行用のデータを作り、処理を呼び出すときにLuaで評価します。スクリプトは `start` や `update` などのコールバックを格納したテーブルを返します。[LuaBindings.cpp](../../Engine/src/scripting/LuaBindings.cpp) がLuaへ公開する関数と型を選び、標準ライブラリからも許可した機能を渡します。

メモリには確保量の上限があり、実行中はLuaのフックで命令数を数えます。呼び出しやフレームで許される命令数を超えるとエラーです。物体に設定した初期状態と、各インスタンスの実行中の状態は分かれています。[SceneScripts.cpp](../../Engine/src/gameplay/SceneScripts.cpp) がアセットから読み込んだコードと、再生中の各物体の状態を扱います。

## キーから姿勢を作る

[TransformAnimationComponent.cpp](../../Engine/src/animation/TransformAnimationComponent.cpp) は現在時刻を挟む2つのキーを探します。線形補間ではその間の割合を使い、STEPなら係数を0にして手前のキーを保持します。位置と拡大率はベクトルを補間し、回転にはクォータニオンの球面線形補間を使います。

[SceneAnimations.cpp](../../Engine/src/animation/SceneAnimations.cpp) はシーン上の変形アニメーションの時計と開始時の姿勢を管理します。モデル内アニメーションは [SceneAnimators.cpp](../../Engine/src/animation/SceneAnimators.cpp) が物体ごとに管理し、[ModelAnimationSampler.cpp](../../Engine/src/animation/ModelAnimationSampler.cpp) がキーからノードの姿勢を求めます。[Skinning.hlsli](../../Engine/resources/shaders/mesh/Skinning.hlsli) では、各頂点に属する骨のうち範囲内で重みが正のものを加算します。

## 粒子の生成と消滅

[SceneParticles.cpp](../../Engine/src/particles/SceneParticles.cpp) は、発生数の端数、各粒子の位置・年齢・寿命、発生器の時計を実行中の状態として管理します。年齢が寿命に達した粒子を取り除き、設定と残り枠に応じて次の粒子を発生させます。描画用のデータは [ParticleRenderer.cpp](../../Engine/src/graphics/render/ParticleRenderer.cpp) に渡します。
