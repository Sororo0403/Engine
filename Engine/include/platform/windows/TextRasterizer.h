#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
struct TextComponent;

/// <summary>
/// DirectWriteの文字配置とWindowsのフォント代替を使い、透過RGBAの文字画像を作る
/// </summary>
class TextRasterizer {
  public:
	static void ValidateFontAsset(const std::filesystem::path &path);

	TextRasterizer();
	~TextRasterizer();
	TextRasterizer(const TextRasterizer &) = delete;
	TextRasterizer &operator=(const TextRasterizer &) = delete;
	/// <summary>
	/// UI矩形へ収まる白い文字の画像を作る。表示色はGPUで掛ける
	/// </summary>
	/// <param name="text">文字・フォント・折り返しと配置の設定</param>
	/// <param name="width">画像の幅。1から4096</param>
	/// <param name="height">画像の高さ。1から4096</param>
	/// <param name="scale">文字サイズへ掛ける正の有限値</param>
	/// <param name="pixels">成功時だけ置き換える、左上から行順のRGBA8。RGBは白、アルファは文字の被覆率</param>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <returns>true:作成成功 false:設定・COM初期化・描画が無効</returns>
	/// <remarks>COMを初期化したスレッドで呼ぶ。ファクトリーは初回利用時だけ作成する</remarks>
	bool Rasterize(const TextComponent &text, int width, int height,
	               float scale, std::vector<std::uint8_t> &pixels,
	               std::string &error);

  private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
