#pragma once

#include "gizmo/TransformGizmo.h"
#include "graphics/ui/ViewportInput.h"

#include <memory>

class ImGuiSystem;
class RenderSystem;
class RenderTexture;

/// <summary>
/// シーン画像の表示領域と描画先テクスチャを管理する
/// </summary>
class SceneViewPanel {
  public:
	/// <summary>
	/// 描画機能への参照を保持する
	/// </summary>
	/// <param name="render">このパネルより長く保持する描画機能</param>
	explicit SceneViewPanel(RenderSystem &render);

	~SceneViewPanel();

	SceneViewPanel(const SceneViewPanel &) = delete;
	SceneViewPanel &operator=(const SceneViewPanel &) = delete;

	/// <summary>
	/// パネルのサイズを反映し、シーン画像の表示UIを構築する。
	/// GPUの利用完了後、描画命令の記録前に呼び出す
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="gizmo">操作切り替えUIを表示するギズモ</param>
	/// <returns>true:成功または非表示 false:描画先の作成失敗</returns>
	bool DrawUI(const ImGuiSystem &ui, TransformGizmo &gizmo);

	/// <summary>
	/// このフレームに表示したScene画像へ変形ギズモを重ねる
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI</param>
	/// <param name="gizmo">表示するギズモ</param>
	/// <param name="view">描画に使用するビュー行列</param>
	/// <param name="projection">描画に使用する投影行列</param>
	/// <param name="objectId">選択物体のID</param>
	/// <param name="transform">編集する変形情報。選択なしの場合はnullptr</param>
	/// <param name="enabled">操作を受け付けるか</param>
	/// <returns>ギズモの操作結果</returns>
	TransformGizmo::Result DrawGizmo(const ImGuiSystem &ui,
	                                 TransformGizmo &gizmo,
	                                 const DirectX::XMFLOAT4X4 &view,
	                                 const DirectX::XMFLOAT4X4 &projection,
	                                 SceneObjectId objectId,
	                                 Transform *transform, bool enabled) const;

	/// <summary>
	/// このフレームで描画するテクスチャを取得する
	/// </summary>
	/// <returns>描画先。パネルが非表示の場合はnullptr</returns>
	RenderTexture *GetRenderTexture() const;

	/// <summary>
	/// Scene画像に対する操作の可否を取得する
	/// </summary>
	/// <returns>DrawUIで取得した状態。非表示の場合は空の状態</returns>
	const ViewportInput &GetInput() const;

	/// <summary>
	/// 描画先を解放する。GPUの利用完了後に呼び出す
	/// </summary>
	void Finalize();

  private:
	// 参照先はSceneViewPanelより長く保持する
	RenderSystem &render_;

	std::unique_ptr<RenderTexture> texture_;
	ViewportInput input_{};

	bool visible_ = false;

	DirectX::XMFLOAT2 imageMinimum_{};
	DirectX::XMFLOAT2 imageSize_{};
};
