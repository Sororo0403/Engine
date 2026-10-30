#pragma once

#include "graphics/MeshData.h"

#include <DirectXMath.h>
#include <cstdint>
#include <filesystem>
#include <vector>

/// <summary>
/// モデルを構成する1つのメッシュの情報
/// </summary>
struct ModelMeshData {
	MeshData mesh;

	// ModelData::materialsの要素番号
	std::uint32_t materialIndex = 0;
};

/// <summary>
/// モデルのテクスチャの保存形式
/// </summary>
enum class ModelTextureSource { NONE, FILE, ENCODED, RGBA8 };

/// <summary>
/// モデルのテクスチャの読み込みに必要な情報
/// </summary>
struct ModelTextureData {
	ModelTextureSource source = ModelTextureSource::NONE;

	// 外部画像のパス
	std::filesystem::path filePath;

	// PNGなどの画像データ、またはRGBA8の画素データ
	std::vector<std::uint8_t> bytes;

	// RGBA8の場合の画像サイズ
	std::uint32_t width = 0;
	std::uint32_t height = 0;
};

/// <summary>
/// モデルファイルから読み込んだ材質の情報
/// </summary>
struct ModelMaterialData {
	DirectX::XMFLOAT4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};

	ModelTextureData baseColorTexture;
};

/// <summary>
/// CPU側で保持するモデルの情報
/// </summary>
struct ModelData {
	std::vector<ModelMeshData> meshes;
	std::vector<ModelMaterialData> materials;
};
