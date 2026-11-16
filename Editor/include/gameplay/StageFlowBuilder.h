#pragma once

#include "physics/AxisAlignedBox.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace StageFlow {
struct Graph;

enum class PortKind { WALK, JUMP };

struct Pose {
	DirectX::XMFLOAT3 position{};
	// 上から見て +Z を +X へ回す90度単位の向き。接続点は区間の外側を向く。
	unsigned quarterTurns = 0;
};

struct Port {
	std::string name;
	PortKind kind = PortKind::WALK;
	Pose pose;
};

struct Module {
	std::string id;
	std::string sectionType;
	std::vector<Port> entries;
	std::vector<Port> exits;
	std::vector<AxisAlignedBox> bounds;
};

struct Placement {
	std::size_t node;
	std::size_t module;
	Pose pose;
};

/// <summary>
/// 区間内の接続点を、区間の配置姿勢からワールドの接続点へ変換する
/// </summary>
Pose Compose(const Pose &local, const Pose &placement);

/// <summary>
/// シード順に区間候補を試し、すべての接続を満たす配置を求める
/// </summary>
/// <param name="graph">開始とゴールを持つ区間の進行グラフ</param>
/// <param name="modules">区間Prefabの接続点と衝突範囲。位置と寸法はPrefabの座標系で指定する</param>
/// <param name="seed">候補の順番を決定するシード</param>
/// <param name="placements">成功時だけ置き換える、区間の配置順と姿勢</param>
/// <param name="error">失敗した区間または設定。成功時は空</param>
bool Build(const Graph &graph, std::span<const Module> modules, std::uint32_t seed,
           std::vector<Placement> &placements, std::string &error);
} // namespace StageFlow
