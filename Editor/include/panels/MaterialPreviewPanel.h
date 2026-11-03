#pragma once

#include "graphics/material/MaterialHandle.h"
#include "graphics/render/RenderObjectHandle.h"
#include <filesystem>
#include <memory>
#include <string>

class ImGuiSystem;
class ModelManager;
class RenderSystem;
class RenderWorld;
class RenderTexture;
struct MaterialAsset;

/// <summary>
/// 独立した箱と描画先で、保存前の材質の見た目を確認する
/// </summary>
class MaterialPreviewPanel {
  public:
	/// <summary>
	/// シーンと独立した描画用の共通機能を参照する。GPUリソースは作成しない
	/// </summary>
	/// <param name="models">Editor用の箱モデルを読み込む管理機能</param>
	/// <param name="render">描画ワールドと描画先の生成・実行機能</param>
	MaterialPreviewPanel(ModelManager &models, RenderSystem &render);
	~MaterialPreviewPanel();

	/// <summary>
	/// プレビューを開き、次のUI更新で画像を読み直す
	/// </summary>
	void Open();

	/// <summary>
	/// 材質の下書きを専用の箱へ反映し、表示する。ファイルや編集シーンは変更しない
	/// </summary>
	/// <param name="ui">開始済みのUI</param>
	/// <param name="draft">有効な下書き。nullptrでは描画を止める</param>
	/// <param name="path">材質ファイル。相対画像を解決する基準に使用する</param>
	/// <remarks>GPU完了後、描画命令の記録前に呼び出す</remarks>
	void DrawUI(const ImGuiSystem &ui, const MaterialAsset *draft,
	            const std::filesystem::path &path);

	/// <summary>
	/// UI構築時に準備した箱を専用の描画先へ描画する
	/// </summary>
	void Draw() const;

	/// <summary>
	/// 材質編集のショートカットを受け付ける画面か確認する
	/// </summary>
	/// <returns>true:プレビューにフォーカスあり false:非表示または別画面</returns>
	bool HasFocus() const;

	/// <summary>
	/// ウィンドウとGPUリソースを解除する。GPUの利用完了後に呼び出す
	/// </summary>
	void Clear();

  private:
	ModelManager &models_;
	RenderSystem &render_;
	std::unique_ptr<RenderWorld> world_;
	std::unique_ptr<RenderTexture> target_;
	RenderObjectHandle object_;
	MaterialHandle material_;
	std::filesystem::path attemptedImage_;
	std::string error_;
	bool open_ = false, focused_ = false, focusRequested_ = false;
	bool reloadRequested_ = false, resourceAttempted_ = false,
	     materialAttempted_ = false;
	bool ready_ = false;
	static constexpr int kPreviewSize = 256;
};
