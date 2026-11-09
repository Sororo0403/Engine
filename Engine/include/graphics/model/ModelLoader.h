#pragma once

#include "graphics/model/ModelData.h"

#include <filesystem>

struct aiScene;
struct aiMaterial;

/// <summary>
/// 静止表示用の形状・材質と、存在する場合はモデル内の骨・クリップを読み込む
/// </summary>
class ModelLoader {
  public:
	/// <summary>
	/// 三角形・材質と、事前変換から分離した骨・ノード・再生キーを読み込む
	/// </summary>
	/// <param name="filePath">読み込むモデルファイルのパス</param>
	/// <param name="modelData">読み込み結果。失敗した場合は変更しない</param>
	/// <returns>true:成功 false:読み込み失敗、または形状が無効</returns>
	static bool Load(const std::filesystem::path &filePath,
	                 ModelData &modelData);

  private:
	/// <summary>
	/// 材質が参照する基本色テクスチャの情報を読み込む
	/// </summary>
	/// <param name="scene">Assimpで読み込んだシーン</param>
	/// <param name="material">読み込む材質</param>
	/// <param name="modelPath">モデルファイルのパス</param>
	/// <param name="textureData">読み込み結果</param>
	/// <returns>true:成功または画像なし false:画像情報が無効、または未対応</returns>
	static bool LoadBaseColorTexture(const aiScene &scene,
	                                 const aiMaterial &material,
	                                 const std::filesystem::path &modelPath,
	                                 TextureData &textureData,
	                                 bool &absoluteReference);
};
