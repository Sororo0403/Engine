#pragma once

#include "animation/ModelAnimationData.h"
struct aiScene;
class GltfAnimationMetadata;

/// <summary>
/// Assimpの一時データから、ライブラリに依存しない骨・形状・キーを複製する
/// </summary>
class ModelAnimationLoader {
  public:
	/// <summary>
	/// 頂点の事前変換より前のシーンを読み、全検証成功後だけ出力を置き換える
	/// </summary>
	/// <param name="scene">左手座標へ変換・三角形化・法線生成済みのシーン</param>
	/// <param name="data">成功時に置き換える共有用データ</param>
	/// <param name="error">失敗理由</param>
	/// <param name="metadata">glTFの元の補間指定。glTF以外はnullptr</param>
	/// <returns>true:成功 false:不正な階層・形状・骨・キー、または未対応の補間</returns>
	static bool Read(const aiScene &scene, ModelAnimationData &data,
	                 std::string &error,
	                 const GltfAnimationMetadata *metadata = nullptr);
};
