#pragma once

#include "graphics/MeshData.h"

#include <filesystem>

class ModelLoader {
  public:
	/// <summary>
	/// モデル内の三角形メッシュをまとめて読み込む
	/// </summary>
	/// <param name="filePath">読み込むモデルファイルのパス</param>
	/// <param name="meshData">読み込み結果。失敗した場合は変更しない</param>
	/// <returns>true:成功 false:読み込み失敗、または形状が無効</returns>
	static bool Load(const std::filesystem::path &filePath, MeshData &meshData);
};
