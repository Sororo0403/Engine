#pragma once

#include <filesystem>
#include <memory>
#include <string>

class ImGuiSystem;
class UITexture;

/// <summary>
/// 選択した1枚の画像を、シーンの編集とは独立して確認する
/// </summary>
class TexturePreviewPanel {
  public:
	TexturePreviewPanel();
	~TexturePreviewPanel();
	TexturePreviewPanel(const TexturePreviewPanel &) = delete;
	TexturePreviewPanel &operator=(const TexturePreviewPanel &) = delete;

	/// <summary>
	/// 次の描画で画像を読み込み、確認用ウィンドウを開く。同じ画像も再読み込みする
	/// </summary>
	/// <param name="path">実行ファイル基準または絶対パス。空は閉じる</param>
	void Open(const std::filesystem::path &path);

	/// <summary>
	/// 表示中の画像と同じ素材を更新した場合に、プレビューの再読み込みを予約する
	/// </summary>
	/// <param name="path">更新した画像のパス</param>
	void RequestReload(const std::filesystem::path &path);

	/// <summary>
	/// 画像の縦横比を保って表示し、再読み込みと終了を受け付ける
	/// </summary>
	/// <param name="ui">フレーム開始済みのUI。GPUの利用完了後、描画命令の記録前に呼び出す</param>
	void Draw(const ImGuiSystem &ui);

	/// <summary>
	/// ウィンドウを閉じ、画像と要求を解除する
	/// </summary>
	void Clear();

  private:
	std::unique_ptr<UITexture> texture_;
	std::filesystem::path path_;
	std::string label_, error_;
	bool open_ = false, focusRequested_ = false, reloadRequested_ = false;
};
