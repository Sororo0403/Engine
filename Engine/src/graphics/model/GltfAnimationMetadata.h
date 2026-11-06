#pragma once

#include "animation/ModelAnimationData.h"
#include <filesystem>
#include <map>
#include <optional>

/// <summary>
/// 使用中のAssimpが省略するglTFの補間指定を、元JSONから保持する
/// </summary>
class GltfAnimationMetadata {
  public:
	/// <summary>
	/// glTFまたはGLBのJSONだけを読み、再生に必要な補間を検証する
	/// </summary>
	/// <param name="path">モデルの絶対パス</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:成功 false:JSON・参照・補間が無効または未対応</returns>
	bool Read(const std::filesystem::path &path, std::string &error);

	/// <summary>
	/// 読み込んだクリップとノード名を照合し、成分ごとのStep指定を戻す
	/// </summary>
	/// <param name="data">同じファイルから抽出した候補</param>
	/// <param name="error">失敗理由</param>
	/// <returns>true:成功 false:元データと読み込み結果が一致しない</returns>
	bool Apply(ModelAnimationData &data, std::string &error) const;

  private:
	struct Modes {
		std::optional<bool> position, rotation, scale;
	};

	std::vector<std::map<std::string, Modes>> clips_;
};
