#include "dialogs/StageFlowDialog.h"

#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include "scene/SceneDocument.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace {
constexpr std::size_t kMaxSections = 64;

/// <summary>
/// 区間IDを一覧から選択する
/// </summary>
bool ChooseNode(const char *label, const StageFlow::Graph &graph, std::string &id) {
	bool changed = false;
	if (ImGui::BeginCombo(label, id.c_str())) {
		for (const auto &node : graph.nodes) {
			if (ImGui::Selectable(node.id.c_str(), id == node.id)) {
				id = node.id;
				changed = true;
			}
		}
		ImGui::EndCombo();
	}
	return changed;
}
} // namespace

void StageFlowDialog::Show(const std::filesystem::path &projectRoot) {
	if (root_ != projectRoot || !loaded_ || !dirty_) {
		const bool switching = root_ != projectRoot;
		root_ = projectRoot;
		project_ = {};
		error_.clear();
		loaded_ = StageFlow::LoadProject(root_, project_, error_);
		dirty_ = false;
		selectedNode_ = 0;
		selectedConnection_ = 0;
		if (switching) {
			newNodeName_.clear();
			sceneName_ = "Skywind Flow";
			seed_ = 0;
			savedPath_.clear();
			placements_.clear();
		}
	}
	message_.clear();
	opening_ = true;
}

StageFlowDialog::Result StageFlowDialog::Draw(
    const ImGuiSystem &ui, SceneDocument &document,
    const std::filesystem::path &scenesDirectory) {
	constexpr auto title = "Stage Flow###StageFlowDialog";
	if (std::exchange(opening_, false)) {
		ui.OpenPopup(title);
	}
	ImGui::SetNextWindowSize({680, 0}, ImGuiCond_Appearing);
	if (!ui.BeginPopupModal(title)) {
		return Result::NONE;
	}

	if (loaded_) {
		DrawGraph();
		if (ImGui::BeginTabBar("Flow Editing")) {
			if (ImGui::BeginTabItem("Sections")) {
				DrawNodes(ui);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Connections")) {
				DrawConnections(ui);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}

		ImGui::Separator();
		ui.InputText("New Scene Name", sceneName_);
		ImGui::InputScalar("Seed", ImGuiDataType_U32, &seed_);
		if (ui.Button("Generate and Save New Scene")) {
			Generate(document, scenesDirectory);
		}
		ui.SameLine();
		ui.BeginDisabled(!dirty_);
		if (ui.Button("Save Graph")) {
			if (StageFlow::SaveGraph(root_, project_.graph, error_)) {
				dirty_ = false;
				message_ = "Saved stage-flow.json.";
			}
		}
		ui.EndDisabled();
	}

	if (!error_.empty()) {
		ImGui::TextWrapped("%s", error_.c_str());
	} else if (!message_.empty()) {
		ImGui::TextWrapped("%s", message_.c_str());
	}
	if (!placements_.empty() && ImGui::CollapsingHeader("Last Generated Sections")) {
		for (const auto &placement : placements_) {
			ui.Text(placement);
		}
	}

	ui.BeginDisabled(savedPath_.empty());
	const bool open = ui.Button("Open Saved Scene");
	ui.EndDisabled();
	ui.SameLine();
	const bool close = ui.Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);
	if (open || close) {
		ui.CloseCurrentPopup();
	}
	ui.EndPopup();
	if (open) {
		return Result::OPEN_SAVED_SCENE;
	}
	return close ? Result::CLOSED : Result::NONE;
}

/// <summary>
/// 進行の深さごとに区間を並べ、分岐と合流を線で表示する
/// </summary>
void StageFlowDialog::DrawGraph() {
	const auto &graph = project_.graph;
	constexpr float nodeWidth = 126;
	constexpr float nodeHeight = 36;
	constexpr float columnStep = 155;
	constexpr float rowStep = 51;
	std::map<std::string, std::size_t> index;
	for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
		index.try_emplace(graph.nodes[i].id, i);
	}
	std::vector<std::size_t> depth(graph.nodes.size());
	std::vector<std::size_t> order;
	std::string validation;
	const bool valid = StageFlow::GetPlacementOrder(graph, order, validation);
	if (valid) {
		for (const auto i : order) {
			for (const auto &link : graph.connections) {
				if (link.from == graph.nodes[i].id) {
					auto &next = depth[index.at(link.to)];
					next = std::max(next, depth[i] + 1);
				}
			}
		}
	}
	std::map<std::size_t, std::size_t> rowCounts;
	std::vector<ImVec2> coordinates;
	ImVec2 extent{nodeWidth + 16, nodeHeight + 16};
	for (const auto column : depth) {
		const float x = 8 + static_cast<float>(column) * columnStep;
		const float y = 8 + static_cast<float>(rowCounts[column]++) * rowStep;
		coordinates.push_back({x, y});
		extent.x = std::max(extent.x, x + nodeWidth + 8);
		extent.y = std::max(extent.y, y + nodeHeight + 8);
	}
	if (ImGui::BeginChild("Flow Preview", {0, 170}, ImGuiChildFlags_Borders,
	                      ImGuiWindowFlags_HorizontalScrollbar)) {
		const auto origin = ImGui::GetCursorScreenPos();
		auto *draw = ImGui::GetWindowDrawList();
		const auto position = [&](std::size_t i) {
			return ImVec2{origin.x + coordinates[i].x, origin.y + coordinates[i].y};
		};
		for (const auto &link : graph.connections) {
			const auto from = index.find(link.from);
			const auto to = index.find(link.to);
			if (from == index.end() || to == index.end() || from == to) {
				continue;
			}
			const auto a = position(from->second);
			const auto b = position(to->second);
			const ImVec2 start{a.x + nodeWidth, a.y + nodeHeight * 0.5f};
			const ImVec2 end{b.x, b.y + nodeHeight * 0.5f};
			const auto color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
			draw->AddBezierCubic(start, {start.x + 24, start.y}, {end.x - 24, end.y},
			                     end, color, 1.5f);
			draw->AddTriangleFilled(end, {end.x - 6, end.y - 4},
			                        {end.x - 6, end.y + 4}, color);
		}
		for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
			const auto min = position(i);
			const ImVec2 max{min.x + nodeWidth, min.y + nodeHeight};
			draw->AddRectFilled(min, max, ImGui::GetColorU32(
			    i == selectedNode_ ? ImGuiCol_HeaderActive : ImGuiCol_FrameBg), 4);
			const auto &node = graph.nodes[i];
			draw->PushClipRect(min, max, true);
			draw->AddText({min.x + 7, min.y + 3}, ImGui::GetColorU32(ImGuiCol_Text), node.id.c_str());
			draw->AddText({min.x + 7, min.y + 19}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
			              node.sectionType.c_str());
			draw->PopClipRect();
		}
		ImGui::Dummy(extent);
	}
	ImGui::EndChild();
	if (!valid) {
		ImGui::TextWrapped("%s", validation.c_str());
	}
}

void StageFlowDialog::DrawNodes(const ImGuiSystem &ui) {
	auto &graph = project_.graph;
	if (ImGui::BeginListBox("##Sections", {-1, 90})) {
		for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
			ImGui::PushID(static_cast<int>(i));
			if (ImGui::Selectable(graph.nodes[i].id.c_str(), selectedNode_ == i)) {
				selectedNode_ = i;
			}
			ImGui::PopID();
		}
		ImGui::EndListBox();
	}
	std::set<std::string> types;
	for (const auto &prefab : project_.prefabs) {
		types.insert(prefab.sectionType);
	}
	if (selectedNode_ < graph.nodes.size()) {
		auto &node = graph.nodes[selectedNode_];
		if (ImGui::BeginCombo("Section Type", node.sectionType.c_str())) {
			for (const auto &type : types) {
				if (ImGui::Selectable(type.c_str(), node.sectionType == type)) {
					node.sectionType = type;
					dirty_ = true;
				}
			}
			ImGui::EndCombo();
		}
		ui.BeginDisabled(graph.nodes.size() <= 2 || node.id == graph.start || node.id == graph.goal);
		if (ui.Button("Remove Selected Section")) {
			std::erase_if(graph.connections, [&](const auto &edge) {
				return edge.from == node.id || edge.to == node.id;
			});
			graph.nodes.erase(graph.nodes.begin() + static_cast<std::ptrdiff_t>(selectedNode_));
			selectedNode_ = std::min(selectedNode_, graph.nodes.size() - 1);
			selectedConnection_ = 0;
			dirty_ = true;
		}
		ui.EndDisabled();
	}
	dirty_ |= ChooseNode("Start", graph, graph.start);
	dirty_ |= ChooseNode("Goal", graph, graph.goal);
	ui.InputText("New Section ID", newNodeName_);
	const bool canAdd = graph.nodes.size() < kMaxSections && !types.empty() &&
	                    !newNodeName_.empty() &&
	                    std::ranges::find(graph.nodes, newNodeName_, &StageFlow::Node::id) == graph.nodes.end();
	ui.BeginDisabled(!canAdd);
	if (ui.Button("Add Section")) {
		graph.nodes.push_back({std::exchange(newNodeName_, {}), *types.begin()});
		selectedNode_ = graph.nodes.size() - 1;
		dirty_ = true;
	}
	ui.EndDisabled();
}

void StageFlowDialog::DrawConnections(const ImGuiSystem &ui) {
	auto &graph = project_.graph;
	if (ImGui::BeginListBox("##Connections", {-1, 95})) {
		for (std::size_t i = 0; i < graph.connections.size(); ++i) {
			const auto &link = graph.connections[i];
			ImGui::PushID(static_cast<int>(i));
			const auto label = link.from + " / " + link.exit + "  ->  " + link.to + " / " + link.entry;
			if (ImGui::Selectable(label.c_str(), selectedConnection_ == i)) {
				selectedConnection_ = i;
			}
			ImGui::PopID();
		}
		ImGui::EndListBox();
	}
	if (selectedConnection_ < graph.connections.size()) {
		auto &link = graph.connections[selectedConnection_];
		dirty_ |= ChooseNode("From Section", graph, link.from);
		dirty_ |= ui.InputText("Exit Name", link.exit);
		dirty_ |= ChooseNode("To Section", graph, link.to);
		dirty_ |= ui.InputText("Entry Name", link.entry);
		if (ui.Button("Remove Selected Connection")) {
			graph.connections.erase(graph.connections.begin() +
			                        static_cast<std::ptrdiff_t>(selectedConnection_));
			selectedConnection_ = 0;
			dirty_ = true;
		}
	}
	if (ui.Button("Add Connection")) {
		graph.connections.push_back({graph.start, "out", graph.goal, "in"});
		selectedConnection_ = graph.connections.size() - 1;
		dirty_ = true;
	}
}

void StageFlowDialog::Generate(SceneDocument &document,
                               const std::filesystem::path &scenesDirectory) {
	error_.clear();
	message_.clear();
	try {
		auto name = PathUtils::FromUtf8(sceneName_);
		if (sceneName_.empty() || !PathUtils::IsValidFileName(name)) {
			throw std::runtime_error("Enter a valid name for the new scene.");
		}
		if (!name.native().ends_with(L".scene.json")) {
			name += L".scene.json";
		}
		const auto destination = scenesDirectory / name;
		if (std::filesystem::exists(destination)) {
			throw std::runtime_error("A scene with that name already exists.");
		}
		Scene generated;
		std::vector<std::string> placements;
		if (!StageFlow::BuildScene(project_, seed_, generated, placements, error_)) {
			return;
		}
		if (!document.SaveCopy(destination, generated, false)) {
			error_ = document.GetError();
			return;
		}
		savedPath_ = destination;
		placements_ = std::move(placements);
		message_ = "Saved " + PathUtils::ToUtf8(destination.filename());
	} catch (const std::exception &error) {
		error_ = error.what();
	}
}
