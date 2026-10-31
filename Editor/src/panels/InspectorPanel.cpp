#include "panels/InspectorPanel.h"

#include "graphics/ui/ImGuiSystem.h"
#include "platform/windows/PathUtils.h"
#include "scene/Scene.h"

#include <DirectXMath.h>
#include <format>

void InspectorPanel::Draw(const ImGuiSystem &ui, Scene &scene,
                          SceneObjectId selectedObjectId) const {
	if (!ui.BeginWindow("Inspector")) {
		ui.EndWindow();
		return;
	}

	SceneObject *object = scene.FindObject(selectedObjectId);

	if (!object) {
		ui.Text("No object selected.");
		ui.EndWindow();
		return;
	}

	ui.Text(object->name);
	ui.Text(std::format("ID: {}", selectedObjectId));

	ui.Separator();

	if (ui.CollapsingHeader("Transform")) {
		Transform &transform = object->transform;

		ui.DragFloat3("Position", transform.translation, 0.01f, -10000.0f,
		              10000.0f);

		DirectX::XMFLOAT3 rotationDegrees{
		    DirectX::XMConvertToDegrees(transform.rotation.x),
		    DirectX::XMConvertToDegrees(transform.rotation.y),
		    DirectX::XMConvertToDegrees(transform.rotation.z),
		};

		// 表示は度数、シーン内の保存値はラジアンで統一する
		if (ui.DragFloat3("Rotation", rotationDegrees, 0.5f, -360.0f, 360.0f,
		                  "%.1f")) {
			transform.rotation = {
			    DirectX::XMConvertToRadians(rotationDegrees.x),
			    DirectX::XMConvertToRadians(rotationDegrees.y),
			    DirectX::XMConvertToRadians(rotationDegrees.z),
			};
		}

		// 今回は正の拡大率を編集対象にする
		ui.DragFloat3("Scale", transform.scale, 0.01f, 0.001f, 1000.0f);
	}

	if (ui.CollapsingHeader("Model")) {
		if (object->modelPath.empty()) {
			ui.Text("No model assigned.");
		} else {
			// パスをUTF-8へ変換して表示する
			ui.Text(PathUtils::ToUtf8(object->modelPath));
		}
	}

	ui.EndWindow();
}
