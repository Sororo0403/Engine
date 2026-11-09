#include "prefab/PrefabInspector.h"

#include "assets/PrefabInstances.h"
#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"

PrefabInspector::Request PrefabInspector::Draw(const ImGuiSystem &ui,
                                               const Scene &scene,
                                               SceneObjectId member,
                                               bool editable) const {
	const auto root = PrefabInstances::GetRoot(scene, member);

	if (!root || !ui.CollapsingHeader("Prefab Instance")) {
		return {};
	}

	const auto &link = *scene.FindObject(root)->prefabLink;
	ui.Text(PathUtils::ToUtf8(link.asset));
	Request request{Action::NONE, root};
	ui.BeginDisabled(!editable);

	if (ui.Button("Select Prefab Root")) {
		request.action = Action::SELECT_ROOT;
	}

	ui.SameLine();

	if (ui.Button("Reveal Prefab Asset")) {
		request.action = Action::REVEAL_ASSET;
	}

	if (ui.Button("Refresh from Prefab")) {
		request.action = Action::REFRESH;
	}

	if (ui.Button("Refresh All Prefab Instances")) {
		request.action = Action::REFRESH_ALL;
	}

	if (ui.Button("Revert Prefab Overrides")) {
		request.action = Action::REVERT;
	}

	if (ui.Button("Save to Prefab Asset")) {
		request.action = Action::SAVE_ASSET;
	}

	if (ui.Button("Unpack Prefab")) {
		request.action = Action::UNPACK;
	}

	ui.EndDisabled();
	return request;
}
