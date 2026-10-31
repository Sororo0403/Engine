#pragma once

#include <memory>

struct ID3D12Resource;

class D3D12Graphics;
class ImGuiSystem;
class RenderSystem;

/// <summary>
/// テクスチャへの描画に使用する色・深度バッファを管理する
/// </summary>
class RenderTexture {
  public:
	/// <summary>
	/// 描画リソースを解放する。GPUの利用完了後に破棄すること
	/// </summary>
	~RenderTexture();

	RenderTexture(const RenderTexture &) = delete;
	RenderTexture &operator=(const RenderTexture &) = delete;

	/// <summary>
	/// 描画領域のサイズを変更する。
	/// GPUの利用完了後、UIへの画像登録と描画命令の記録前に呼び出す
	/// </summary>
	/// <param name="width">変更後の幅。1以上16384以下</param>
	/// <param name="height">変更後の高さ。1以上16384以下</param>
	/// <returns>true:成功 false:サイズが無効、または作成失敗</returns>
	bool Resize(int width, int height);

	/// <summary>
	/// 描画領域の幅を取得する
	/// </summary>
	/// <returns>幅。未作成の場合は0</returns>
	int GetWidth() const;

	/// <summary>
	/// 描画領域の高さを取得する
	/// </summary>
	/// <returns>高さ。未作成の場合は0</returns>
	int GetHeight() const;

  private:
	friend class RenderSystem;
	friend class ImGuiSystem;

	explicit RenderTexture(D3D12Graphics &graphics);

	/// <summary>
	/// テクスチャを描画先に設定し、色と深度をクリアする
	/// </summary>
	/// <param name="clearColor">赤・緑・青・アルファの4要素</param>
	/// <returns>true:成功 false:未作成、または描画基盤が無効</returns>
	bool Begin(const float (&clearColor)[4]);

	/// <summary>
	/// テクスチャを読み取り用に戻し、バックバッファを描画先に設定する
	/// </summary>
	void End();

	/// <summary>
	/// UI表示に使用する色バッファを取得する
	/// </summary>
	/// <returns>色バッファへの借用ポインター</returns>
	ID3D12Resource *GetColorResource() const;

	/// <summary>
	/// 指定した描画基盤を使用しているか取得する
	/// </summary>
	/// <param name="graphics">確認する描画基盤</param>
	/// <returns>true:一致 false:不一致</returns>
	bool Uses(const D3D12Graphics &graphics) const;

	struct Impl;
	std::unique_ptr<Impl> impl_;
};
