#pragma once

#include "graphics/material/MaterialManager.h"
#include "graphics/render/RenderWorld.h"

#include <memory>

class D3D12Graphics;
class MeshRenderer;
class WinApp;
class RenderTexture;
class DebugLineRenderer;

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
	void Draw(const RenderWorld &world, RenderTexture &target,
	          const float (&clearColor)[4]) const;

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
	void Draw(const RenderWorld &world) const;

	/// <summary>
	/// 描画基盤で共有する、モデルから独立した材質管理を取得する
	/// </summary>
	/// <returns>GPU完了後に利用する材質管理</returns>
	MaterialManager &GetMaterials();

  private:
	MaterialManager materials_;
	// 参照先はRenderSystemより長く保持する
	D3D12Graphics &graphics_;
	MeshRenderer &renderer_;
	DebugLineRenderer &debugRenderer_;
	const WinApp &app_;
};
