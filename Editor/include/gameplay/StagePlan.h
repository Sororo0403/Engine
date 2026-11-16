#pragma once

#include <DirectXMath.h>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

class Scene;

namespace StagePlan {
enum class PlatformStyle { STONE, TURF };
enum class WindRequirement { UPDRAFT, CROSSWIND };

struct SectionBrief {
	std::string id;
	std::string name;
	WindRequirement wind = WindRequirement::UPDRAFT;
	bool windIsRequired = true;
	std::string intent;
};

struct Platform {
	std::string id;
	PlatformStyle style;
	DirectX::XMFLOAT3 topCenter;
	DirectX::XMFLOAT3 size;
};

struct Wind {
	DirectX::XMFLOAT3 center;
	DirectX::XMFLOAT3 size;
	DirectX::XMFLOAT3 velocity;
	bool airborneOnly;
};

struct Section {
	std::string id;
	std::vector<Platform> landings;
	std::vector<Wind> winds;
};

struct Layout {
	Platform start;
	std::vector<Section> sections;
};

/// <summary>
/// ステージ案または指定した一区間の改案を読み込み、範囲・足場・接続を検証する
/// </summary>
/// <param name="text">クリップボードまたはファイルから取得した配置案のJSON</param>
/// <param name="brief">エディターで指定した進行順と必須条件</param>
/// <param name="replaceSection">改案の対象ID。空なら全区間を新規に読み込む</param>
/// <param name="layout">現在の案。検証成功時だけ差し替える</param>
/// <param name="error">失敗した区間と理由。成功時は空</param>
/// <returns>true:読み込み成功 false:案が無効</returns>
bool Import(std::string_view text, std::span<const SectionBrief> brief,
            std::optional<std::string_view> replaceSection, Layout &layout,
            std::string &error);

/// <summary>
/// クリップボードにコピーしてChatGPTへ渡す、区間の条件と返答形式を作る
/// </summary>
/// <param name="brief">エディターで指定した区間の順番と遊び</param>
/// <param name="previous">改案時に参照する配置。全区間の設計時はnullptr</param>
/// <param name="replaceSection">改案の対象ID。空なら全区間を設計する</param>
/// <returns>プロンプト。区間指定が無効な場合は空</returns>
std::string MakePrompt(std::span<const SectionBrief> brief, const Layout *previous,
                       std::optional<std::string_view> replaceSection);

/// <summary>
/// 検証済みの配置案から、足場・風・試遊用の主人公を持つシーンを作る
/// </summary>
/// <param name="layout">取り込みと検証を通過した配置案</param>
/// <param name="projectRoot">試作で使用する風のスクリプトを含むプロジェクト</param>
/// <param name="output">成功時だけ置き換えるシーン</param>
/// <param name="error">失敗理由。成功時は空</param>
/// <returns>true:生成成功 false:素材または配置が無効</returns>
bool BuildScene(const Layout &layout, const std::filesystem::path &projectRoot,
                Scene &output, std::string &error);
} // namespace StagePlan
