#include "panels/InspectorPanel.h"

#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"

#include <DirectXMath.h>
#include <format>

bool InspectorPanel::Draw(const ImGuiSystem &ui, Scene &scene,
                          SceneObjectId selectedObjectId) const {
	if (!ui.BeginWindow("Inspector")) {
		ui.EndWindow();
		return false;
	}

	SceneObject *object = scene.FindObject(selectedObjectId);

	if (!object) {
		ui.Text("No object selected.");
		ui.EndWindow();
		return false;
	}

	bool changed = false;

	ui.Text(object->name);
	ui.Text(std::format("ID: {}", selectedObjectId));
	ui.Separator();

	if (ui.CollapsingHeader("Transform")) {
		Transform &transform = object->transform;

		if (ui.DragFloat3("Position", transform.translation, 0.01f, -10000.0f,
		                  10000.0f)) {
			changed = true;
		}

		DirectX::XMFLOAT3 rotationDegrees{
		    DirectX::XMConvertToDegrees(transform.rotation.x),
		    DirectX::XMConvertToDegrees(transform.rotation.y),
		    DirectX::XMConvertToDegrees(transform.rotation.z),
		};

		// 表示は度数、シーンの保持値はラジアン
		if (ui.DragFloat3("Rotation", rotationDegrees, 0.5f, -360.0f, 360.0f,
		                  "%.1f")) {
			transform.rotation = {
			    DirectX::XMConvertToRadians(rotationDegrees.x),
			    DirectX::XMConvertToRadians(rotationDegrees.y),
			    DirectX::XMConvertToRadians(rotationDegrees.z),
			};

			changed = true;
		}

		if (ui.DragFloat3("Scale", transform.scale, 0.01f, 0.001f, 1000.0f)) {
			changed = true;
		}
	}

	if (ui.CollapsingHeader("Model")) {
		if (object->modelPath.empty()) {
			ui.Text("No model assigned.");
		} else {
			ui.Text(PathUtils::ToUtf8(object->modelPath));
		}
	}

	ui.EndWindow();
	return changed;
}
