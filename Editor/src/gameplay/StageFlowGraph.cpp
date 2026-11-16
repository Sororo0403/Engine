#include "gameplay/StageFlowGraph.h"

#include <algorithm>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace StageFlow {
bool GetPlacementOrder(const Graph &graph, std::vector<std::size_t> &order,
                       std::string &error) {
	std::map<std::string_view, std::size_t, std::less<>> index;
	for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
		const auto &node = graph.nodes[i];
		if (node.id.empty() || node.sectionType.empty() || !index.emplace(node.id, i).second) {
			error = "Each section needs a unique ID and a section type.";
			return false;
		}
	}
	if (graph.start == graph.goal || !index.contains(graph.start) || !index.contains(graph.goal)) {
		error = "Choose different, existing start and goal sections.";
		return false;
	}

	const auto start = index.at(graph.start);
	const auto goal = index.at(graph.goal);
	std::vector<std::vector<std::size_t>> successors(graph.nodes.size());
	std::vector<std::size_t> incoming(graph.nodes.size());
	using Port = std::pair<std::string_view, std::string_view>;
	std::set<Port> usedExits;
	std::set<Port> usedEntries;
	for (const auto &link : graph.connections) {
		const auto from = index.find(link.from);
		const auto to = index.find(link.to);
		if (from == index.end() || to == index.end() || from == to ||
		    link.exit.empty() || link.entry.empty()) {
			error = "Invalid connection from '" + link.from + "' to '" + link.to + "'.";
			return false;
		}
		if (!usedExits.emplace(link.from, link.exit).second) {
			error = "Exit '" + link.from + "/" + link.exit + "' is connected more than once.";
			return false;
		}
		if (!usedEntries.emplace(link.to, link.entry).second) {
			error = "Entry '" + link.to + "/" + link.entry + "' is connected more than once.";
			return false;
		}
		successors[from->second].push_back(to->second);
		++incoming[to->second];
	}

	for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
		if ((incoming[i] == 0) != (i == start) ||
		    successors[i].empty() != (i == goal)) {
			error = "Section '" + graph.nodes[i].id + "' must belong to a route from start to goal.";
			return false;
		}
		std::ranges::sort(successors[i]);
	}

	std::vector<std::size_t> candidate{start};
	for (std::size_t i = 0; i < candidate.size(); ++i) {
		for (const auto next : successors[candidate[i]]) {
			if (--incoming[next] == 0) {
				candidate.push_back(next);
			}
		}
	}
	if (candidate.size() != graph.nodes.size()) {
		error = "Section connections contain a cycle.";
		return false;
	}

	order.swap(candidate);
	error.clear();
	return true;
}
} // namespace StageFlow
