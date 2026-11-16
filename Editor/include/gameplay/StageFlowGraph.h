#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace StageFlow {
struct Node {
	std::string id;
	std::string sectionType;
};

struct Connection {
	std::string from;
	std::string exit;
	std::string to;
	std::string entry;
};

struct Graph {
	std::string start;
	std::string goal;
	std::vector<Node> nodes;
	std::vector<Connection> connections;
};

/// <summary>
/// 開始からゴールへつながる進行グラフを検証し、接続元を先に配置する順序を求める
/// </summary>
/// <param name="graph">区間と入口・出口の接続を記録したグラフ</param>
/// <param name="order">成功時だけ置き換える、nodesの添字の配置順</param>
/// <param name="error">失敗時の理由。成功時は空</param>
/// <returns>true:有効 false:接続の不備または循環がある</returns>
bool GetPlacementOrder(const Graph &graph, std::vector<std::size_t> &order,
                       std::string &error);
} // namespace StageFlow
