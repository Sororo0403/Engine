#include "gameplay/StageFlowBuilder.h"

#include "gameplay/StageFlowGraph.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <string_view>
#include <utility>

namespace StageFlow {
namespace {
using DirectX::XMFLOAT3;

constexpr float kSeamTolerance = 0.005f;
constexpr std::size_t kMaxNodes = 64;
constexpr std::size_t kMaxAttempts = 50000;

XMFLOAT3 Rotate(const XMFLOAT3 &value, unsigned turns) {
	switch (turns % 4) {
	case 1:
		return {value.z, value.y, -value.x};
	case 2:
		return {-value.x, value.y, -value.z};
	case 3:
		return {-value.z, value.y, value.x};
	default:
		return value;
	}
}

Pose Align(const Pose &entry, const Pose &exit) {
	const unsigned turns = (exit.quarterTurns + 6 - entry.quarterTurns) % 4;
	const auto offset = Rotate(entry.position, turns);
	return {{exit.position.x - offset.x, exit.position.y - offset.y,
	         exit.position.z - offset.z}, turns};
}

bool SameSeam(const Pose &a, const Pose &b) {
	return a.quarterTurns == b.quarterTurns &&
	       std::abs(a.position.x - b.position.x) <= kSeamTolerance &&
	       std::abs(a.position.y - b.position.y) <= kSeamTolerance &&
	       std::abs(a.position.z - b.position.z) <= kSeamTolerance;
}

bool IsFinite(const XMFLOAT3 &position) {
	return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
}

bool ValidPorts(std::span<const Port> ports) {
	std::set<std::string_view> names;
	return std::ranges::all_of(ports, [&](const Port &port) {
		return !port.name.empty() && port.pose.quarterTurns < 4 && IsFinite(port.pose.position) &&
		       names.insert(port.name).second;
	});
}

AxisAlignedBox ToWorldBounds(const AxisAlignedBox &bounds, const Pose &pose) {
	const auto lo = Compose({bounds.minimum}, pose).position;
	const auto hi = Compose({bounds.maximum}, pose).position;
	return {{std::min(lo.x, hi.x), std::min(lo.y, hi.y), std::min(lo.z, hi.z)},
	        {std::max(lo.x, hi.x), std::max(lo.y, hi.y), std::max(lo.z, hi.z)}};
}

bool Overlaps(const AxisAlignedBox &a, const AxisAlignedBox &b) {
	constexpr float tolerance = 0.0001f;
	return std::min(a.maximum.x, b.maximum.x) - std::max(a.minimum.x, b.minimum.x) > tolerance &&
	       std::min(a.maximum.y, b.maximum.y) - std::max(a.minimum.y, b.minimum.y) > tolerance &&
	       std::min(a.maximum.z, b.maximum.z) - std::max(a.minimum.z, b.minimum.z) > tolerance;
}

const Port *FindPort(const std::vector<Port> &ports, std::string_view name) {
	const auto found = std::ranges::find(ports, name, &Port::name);
	return found == ports.end() ? nullptr : &*found;
}

struct Edge {
	std::size_t source;
	const Connection *link;
};

struct Choices {
	std::vector<std::size_t> modules;
	std::vector<Edge> incoming;
	std::vector<const Connection *> outgoing;
};

struct Placed {
	std::size_t module;
	Pose pose;
	std::vector<AxisAlignedBox> bounds;
};

/// <summary>
/// 配置済みの接続元をすべて照合し、合流する入口も同じ区間姿勢で一致するか調べる
/// </summary>
std::optional<Pose> Match(const Choices &choices, const Module &module,
                          std::span<const Module> modules,
                          const std::vector<std::optional<Placed>> &placed) {
	for (const auto *link : choices.outgoing) {
		if (!FindPort(module.exits, link->exit)) {
			return std::nullopt;
		}
	}
	std::optional<Pose> result;
	for (const auto &edge : choices.incoming) {
		const auto &previous = placed[edge.source].value();
		const auto *exit = FindPort(modules[previous.module].exits, edge.link->exit);
		const auto *entry = FindPort(module.entries, edge.link->entry);
		if (!exit || !entry || exit->kind != entry->kind) {
			return std::nullopt;
		}
		const auto required = Align(entry->pose, Compose(exit->pose, previous.pose));
		if (!IsFinite(required.position) || (result && !SameSeam(*result, required))) {
			return std::nullopt;
		}
		result = required;
	}
	return result.value_or(Pose{});
}

/// <summary>
/// 置けない区間から引き返し、前の区間の候補を含めて選び直す
/// </summary>
bool Search(std::span<const std::size_t> order, std::size_t depth,
            std::span<const Choices> choices, std::span<const Module> modules,
            std::vector<std::optional<Placed>> &placed, std::size_t &attempts,
            std::size_t &deepest) {
	if (depth == order.size()) {
		return true;
	}
	deepest = std::max(deepest, depth);
	const auto node = order[depth];
	for (const auto variant : choices[node].modules) {
		if (attempts++ >= kMaxAttempts) {
			return false;
		}
		const auto &module = modules[variant];
		const auto pose = Match(choices[node], module, modules, placed);
		if (!pose) {
			continue;
		}

		Placed candidate{variant, *pose, {}};
		for (const auto &box : module.bounds) {
			candidate.bounds.push_back(ToWorldBounds(box, *pose));
		}
		const auto blocked = std::ranges::any_of(candidate.bounds, [&](const AxisAlignedBox &box) {
			return !IsFinite(box.minimum) || !IsFinite(box.maximum) ||
			       std::ranges::any_of(placed, [&](const auto &previous) {
				       return previous && std::ranges::any_of(previous->bounds, [&](const auto &other) {
					       return Overlaps(box, other);
				       });
			       });
		});
		if (blocked) {
			continue;
		}
		placed[node] = std::move(candidate);
		if (Search(order, depth + 1, choices, modules, placed, attempts, deepest)) {
			return true;
		}
		placed[node].reset();
	}
	return false;
}
} // namespace

Pose Compose(const Pose &local, const Pose &placement) {
	const auto point = Rotate(local.position, placement.quarterTurns);
	return {{point.x + placement.position.x, point.y + placement.position.y,
	         point.z + placement.position.z},
	        (local.quarterTurns + placement.quarterTurns) % 4};
}

bool Build(const Graph &graph, std::span<const Module> modules, std::uint32_t seed,
           std::vector<Placement> &placements, std::string &error) {
	if (graph.nodes.size() > kMaxNodes) {
		error = "A stage can contain up to 64 sections.";
		return false;
	}
	std::vector<std::size_t> order;
	if (!GetPlacementOrder(graph, order, error)) {
		return false;
	}

	std::vector<std::size_t> catalog(modules.size());
	std::iota(catalog.begin(), catalog.end(), std::size_t{0});
	std::set<std::string_view> ids;
	for (const auto &module : modules) {
		if (module.id.empty() || module.sectionType.empty() || !ids.insert(module.id).second ||
		    !ValidPorts(module.entries) || !ValidPorts(module.exits) || module.bounds.empty() ||
		    !std::ranges::all_of(module.bounds, [](const AxisAlignedBox &bounds) {
			    return IsFinite(bounds.minimum) && IsFinite(bounds.maximum) &&
			           bounds.minimum.x < bounds.maximum.x && bounds.minimum.y < bounds.maximum.y &&
			           bounds.minimum.z < bounds.maximum.z;
		    })) {
			error = "Invalid ports or collision bounds in section prefab '" + module.id + "'.";
			return false;
		}
	}
	std::ranges::sort(catalog, {}, [&](std::size_t i) -> const std::string & {
		return modules[i].id;
	});

	std::vector<Choices> choices(graph.nodes.size());
	std::mt19937 random(seed);
	for (const auto node : order) {
		auto &variants = choices[node].modules;
		for (const auto index : catalog) {
			if (modules[index].sectionType == graph.nodes[node].sectionType) {
				variants.push_back(index);
			}
		}
		if (variants.empty()) {
			error = "No prefab candidates for section '" + graph.nodes[node].id + "'.";
			return false;
		}
		std::shuffle(variants.begin(), variants.end(), random);
	}
	for (const auto &link : graph.connections) {
		const auto from = std::ranges::find(graph.nodes, link.from, &Node::id) - graph.nodes.begin();
		const auto to = std::ranges::find(graph.nodes, link.to, &Node::id) - graph.nodes.begin();
		const auto source = static_cast<std::size_t>(from);
		choices[source].outgoing.push_back(&link);
		choices[static_cast<std::size_t>(to)].incoming.push_back({source, &link});
	}
	for (auto &choice : choices) {
		std::ranges::sort(choice.incoming, {}, &Edge::source);
	}

	std::vector<std::optional<Placed>> placed(graph.nodes.size());
	std::size_t attempts = 0;
	std::size_t deepest = 0;
	if (!Search(order, 0, choices, modules, placed, attempts, deepest)) {
		error = (attempts > kMaxAttempts ? "Search limit reached at section '" :
		                                  "No matching, unobstructed placement for section '") +
		        graph.nodes[order[deepest]].id + "'.";
		return false;
	}
	std::vector<Placement> result;
	for (const auto node : order) {
		const auto &entry = placed[node].value();
		result.push_back({node, entry.module, entry.pose});
	}
	placements.swap(result);
	error.clear();
	return true;
}
} // namespace StageFlow
