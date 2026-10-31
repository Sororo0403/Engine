#pragma once

#include <memory>

class DebugUI;
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
	/// <returns>true:成功または非表示 false:描画先の作成失敗</returns>
	bool DrawUI(const DebugUI &ui);

	/// <summary>
	/// このフレームで描画するテクスチャを取得する
	/// </summary>
	/// <returns>描画先。パネルが非表示の場合はnullptr</returns>
	RenderTexture *GetRenderTexture() const;

	/// <summary>
	/// 描画先を解放する。GPUの利用完了後に呼び出す
	/// </summary>
	void Finalize();

  private:
	// 参照先はSceneViewPanelより長く保持する
	RenderSystem &render_;

	std::unique_ptr<RenderTexture> texture_;
	bool visible_ = false;
};
