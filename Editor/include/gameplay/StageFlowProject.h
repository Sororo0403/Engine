#pragma once

#include "gameplay/StageFlowGraph.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class Scene;

namespace StageFlow {
struct PrefabEntry {
	std::string id;
	std::string sectionType;
	std::filesystem::path path;
};

struct Decoration {
	std::string marker;
	std::filesystem::path model;
	float scale = 1.0f;
	float yawDegrees = 0.0f;
};

struct Project {
	Graph graph;
	std::vector<PrefabEntry> prefabs;
	std::vector<Decoration> decorations;
	std::filesystem::path airflowScript;
};

/// <summary>
/// プロジェクトのフローグラフ・区間Prefab候補・装飾テーマを読み込む
/// </summary>
/// <returns>成功時だけprojectを置き換える。失敗時は理由を返す</returns>
bool LoadProject(const std::filesystem::path &projectRoot, Project &project, std::string &error);

/// <summary>
/// 区間を選んで配置し、飾りと試遊用の主人公・カメラ・風を持つ独立したシーンへ組み立てる
/// </summary>
/// <returns>成功時だけsceneと区間名の一覧を置き換える。失敗時は元のシーンを保持する</returns>
bool BuildScene(const Project &project, std::uint32_t seed, Scene &scene,
                std::vector<std::string> &variants, std::string &error);
} // namespace StageFlow
