#pragma once

#include "scene/StageDecorationLayout.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

class Scene;

/// <summary>
/// 編集シーンから取得した、装飾生成に使う地形と風の範囲
/// </summary>
struct StageDecorationSource {
	std::vector<AxisAlignedBox> route;
	std::vector<StageDecorationLayout::WindArea> windAreas;
	std::vector<AxisAlignedBox> obstacles;

	/// <summary>
	/// 足場モデルと風を制御するスクリプトを手掛かりに、配置に必要な領域を抽出する
	/// </summary>
	/// <param name="scene">読み取り専用の編集シーン</param>
	/// <param name="platformPrefix">進行順に登録された足場のモデル名の接頭辞</param>
	/// <param name="windScript">風の設定を持つスクリプトへの絶対パス。空なら風を抽出しない</param>
	/// <param name="error">失敗理由。成功時は空</param>
	/// <returns>true:足場を取得した false:足場が不足または設定が無効。結果は保持する</returns>
	bool Read(const Scene &scene, std::string_view platformPrefix,
	          const std::filesystem::path &windScript, std::string &error);
};
