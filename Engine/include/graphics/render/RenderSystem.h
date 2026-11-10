#pragma once

#include "graphics/material/MaterialManager.h"
#include "graphics/render/RenderWorld.h"

#include "ui/UIButtonVisual.h"
#include <array>
#include <memory>
#include <span>

class D3D12Graphics;
class MeshRenderer;
class WinApp;
class RenderTexture;
class DebugLineRenderer;
class CanvasRenderer;
class ParticleRenderer;
class Scene;

/// <summary>
/// 描画ワールドの生成・描画準備・描画の実行を担当する
/// </summary>
class RenderSystem {
  public:
	/// <summary>
	/// 描画に使用する共通機能への参照を保持する。GPUリソースは作成しない
	/// </summary>
	/// <param name="graphics">描画基盤</param>
	/// <param name="renderer">メッシュ描画クラス</param>
	/// <param name="debugRenderer">線描画クラス</param>
	/// <param name="app">表示先のアプリケーション</param>
	RenderSystem(D3D12Graphics &graphics, MeshRenderer &renderer,
	             DebugLineRenderer &debugRenderer, const WinApp &app);

	RenderSystem(const RenderSystem &) = delete;
	RenderSystem &operator=(const RenderSystem &) = delete;

	/// <summary>
	/// テクスチャへの描画先を作成する
	/// </summary>
	/// <param name="width">描画領域の幅</param>
	/// <param name="height">描画領域の高さ</param>
	/// <returns>成功:描画先 失敗:nullptr</returns>
	std::unique_ptr<RenderTexture> CreateRenderTexture(int width, int height);

	/// <summary>
	/// 指定した描画サイズでGPU用データを更新する
	/// </summary>
	/// <param name="world">更新するワールド</param>
	/// <param name="width">描画領域の幅</param>
	/// <param name="height">描画領域の高さ</param>
	/// <returns>true:成功 false:描画基盤が異なる、または更新失敗</returns>
	bool Update(RenderWorld &world, int width, int height);

	/// <summary>
	/// テクスチャへ描画し、描画先をバックバッファへ戻す
	/// </summary>
	/// <param name="world">描画するワールド</param>
	/// <param name="target">描画先</param>
	/// <param name="clearColor">赤・緑・青・アルファの4要素</param>
	/// <param name="canvas">3Dの前後に描画するCanvas。nullptrはCanvasなし</param>
	/// <param name="particles">3DとUIの間へ合成する粒子。nullptrはなし</param>
	void Draw(const RenderWorld &world, RenderTexture &target,
	          const float (&clearColor)[4],
	          const CanvasRenderer *canvas = nullptr,
	          const ParticleRenderer *particles = nullptr) const;

	/// <summary>
	/// シーン専用の描画ワールドを作成する
	/// </summary>
	/// <returns>成功:初期化済みのワールド 失敗:nullptr</returns>
	std::unique_ptr<RenderWorld> CreateWorld();

	/// <summary>
	/// ウィンドウの描画サイズでGPU用データを更新する
	/// </summary>
	/// <param name="world">更新するワールド</param>
	/// <returns>true:成功 false:描画基盤が異なる、または更新失敗</returns>
	bool Update(RenderWorld &world);

	/// <summary>
	/// 現在の描画先へワールドの描画命令を記録する
	/// </summary>
	/// <param name="world">描画するワールド</param>
	/// <param name="canvas">3Dの前後に描画するCanvas。nullptrはCanvasなし</param>
	/// <param name="particles">3DとUIの間へ合成する粒子。nullptrはなし</param>
	void Draw(const RenderWorld &world, const CanvasRenderer *canvas = nullptr,
	          const ParticleRenderer *particles = nullptr) const;

	/// <summary>
	/// 描画基盤で共有する、モデルから独立した材質管理を取得する
	/// </summary>
	/// <returns>GPU完了後に利用する材質管理</returns>
	MaterialManager &GetMaterials();

	/// <summary>
	/// 同じ描画基盤を使用する、遅延初期化のCanvas描画を作る
	/// </summary>
	/// <param name="skipUnavailable">true:編集用に未設定・読み込み不能のUI要素を省略する</param>
	/// <returns>成功:描画クラス 失敗:nullptr</returns>
	std::unique_ptr<CanvasRenderer> CreateCanvas(bool skipUnavailable = false);
	/// <summary>
	/// Canvasの素材と配置をGPU完了後に準備する
	/// </summary>
	/// <param name="canvas">この描画基盤から作成したCanvas描画</param>
	/// <param name="scene">表示するシーン</param>
	/// <param name="width">描画幅。高さと共に0の場合はウィンドウサイズ</param>
	/// <param name="height">描画高さ</param>
	/// <param name="visuals">再生中のボタン表示</param>
	/// <returns>true:成功 false:配置・素材・描画準備が無効</returns>
	bool UpdateCanvas(CanvasRenderer &canvas, const Scene &scene, int width = 0,
	                  int height = 0,
	                  std::span<const UIButtonVisual> visuals = {},
	                  bool includeOverlay = true);

	/// <summary>
	/// 実行ウィンドウの現在のクライアント寸法を取得する
	/// </summary>
	/// <returns>横・縦のピクセル数。リサイズ後も実際の描画寸法を返す</returns>
	std::array<int, 2> GetWindowSize() const;

	/// <summary>
	/// 同じ描画基盤と画像管理を使う、遅延初期化の粒子描画を作成する
	/// </summary>
	/// <returns>この描画基盤より先に破棄する描画クラス</returns>
	std::unique_ptr<ParticleRenderer> CreateParticles();

  private:
	MaterialManager materials_;
	// 参照先はRenderSystemより長く保持する
	D3D12Graphics &graphics_;
	MeshRenderer &renderer_;
	DebugLineRenderer &debugRenderer_;
	const WinApp &app_;
};
