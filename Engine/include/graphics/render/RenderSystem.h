#pragma once

#include "graphics/render/RenderWorld.h"

#include <memory>

class D3D12Graphics;
class MeshRenderer;
class WinApp;

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
	/// <param name="app">表示先のアプリケーション</param>
	RenderSystem(D3D12Graphics &graphics, MeshRenderer &renderer,
	             const WinApp &app);

	RenderSystem(const RenderSystem &) = delete;
	RenderSystem &operator=(const RenderSystem &) = delete;

	/// <summary>
	/// シーン専用の描画ワールドを作成する
	/// </summary>
	/// <returns>成功:初期化済みのワールド 失敗:nullptr</returns>
	std::unique_ptr<RenderWorld> CreateWorld();

	/// <summary>
	/// GPU用データを更新する。全ての編集後、BeginFrame前に呼び出す
	/// </summary>
	/// <param name="world">更新するワールド</param>
	/// <returns>true:成功 false:描画基盤が異なる、または更新失敗</returns>
	bool Update(RenderWorld &world);

	/// <summary>
	/// 描画命令を記録する。Update成功後、BeginFrameとEndFrameの間に一度呼び出す
	/// </summary>
	/// <param name="world">描画するワールド</param>
	void Draw(const RenderWorld &world) const;

  private:
	// 参照先はRenderSystemより長く保持する
	D3D12Graphics &graphics_;
	MeshRenderer &renderer_;
	const WinApp &app_;
};
