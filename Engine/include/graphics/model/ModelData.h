#pragma once

#include "graphics/material/TextureData.h"
#include "graphics/mesh/MeshData.h"

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
/// モデルファイルから読み込んだ材質の情報
/// </summary>
struct ModelMaterialData {
	DirectX::XMFLOAT4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};

	TextureData baseColorTexture;
};

/// <summary>
/// CPU側で保持するモデルの情報
/// </summary>
struct ModelData {
	std::vector<ModelMeshData> meshes;
	std::vector<ModelMaterialData> materials;
};
