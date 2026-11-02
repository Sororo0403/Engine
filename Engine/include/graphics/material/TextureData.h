#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

/// <summary>
/// モデルのテクスチャの保存形式
/// </summary>
enum class TextureSource { NONE, FILE, ENCODED, RGBA8 };

/// <summary>
/// モデルのテクスチャの読み込みに必要な情報
/// </summary>
struct TextureData {
	TextureSource source = TextureSource::NONE;

	// 外部画像のパス
	std::filesystem::path filePath;

	// PNGなどの画像データ、またはRGBA8の画素データ
	std::vector<std::uint8_t> bytes;

	// RGBA8の場合の画像サイズ
	std::uint32_t width = 0;
	std::uint32_t height = 0;
};
