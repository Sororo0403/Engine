#pragma once

#include <filesystem>
#include <memory>

struct ID3D12Resource;
class D3D12Graphics;
class ImGuiSystem;

/// <summary>
/// UIで表示する読み込み済み画像を保持する。作成はImGuiSystemから行う
/// </summary>
class UITexture {
  public:
	~UITexture();
	UITexture(const UITexture &) = delete;
	UITexture &operator=(const UITexture &) = delete;

	/// <summary>
	/// 画像の幅を取得する
	/// </summary>
	/// <returns>幅（ピクセル）。未読み込みは0</returns>
	int GetWidth() const;

	/// <summary>
	/// 画像の高さを取得する
	/// </summary>
	/// <returns>高さ（ピクセル）。未読み込みは0</returns>
	int GetHeight() const;

  private:
	friend class ImGuiSystem;
	UITexture();

	/// <summary>
	/// 画像を読み込み、転送完了まで待機する
	/// </summary>
	/// <param name="graphics">使用する描画基盤</param>
	/// <param name="path">読み込む画像</param>
	/// <returns>true:成功 false:未初期化または読み込み失敗</returns>
	bool Load(D3D12Graphics &graphics, const std::filesystem::path &path);

	/// <summary>
	/// 指定した描画基盤と同じデバイスの画像か確認する
	/// </summary>
	/// <param name="graphics">確認する描画基盤</param>
	/// <returns>true:一致 false:未読み込みまたは異なるデバイス</returns>
	bool Uses(const D3D12Graphics &graphics) const;

	/// <summary>
	/// UI表示に使用する画像リソースを取得する
	/// </summary>
	/// <returns>借用ポインタ。未読み込みはnullptr</returns>
	ID3D12Resource *GetResource() const;

	struct Impl;
	std::unique_ptr<Impl> impl_;
};
