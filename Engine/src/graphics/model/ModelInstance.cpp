#include "graphics/model/ModelInstance.h"
#include "core/Logger.h"
#include "graphics/DirectionalLight.h"
#include "graphics/camera/Camera.h"
#include "graphics/model/Model.h"
#include "graphics/render/MeshRenderer.h"
#include "math/TransformUtils.h"

#include <exception>
#include <format>

bool ModelInstance::Initialize(ID3D12Device *device, const Model &model) {
	if (!device || model.GetMeshCount() == 0 || model.GetMaterialCount() == 0) {
		Logger::Error("ModelInstance::Initialize: "
		              "引数が無効、またはモデルが未初期化です。");
		return false;
	}

	if (model_ || !materials_.empty() || !materialData_.empty() ||
	    !meshInstances_.empty()) {
		Logger::Error("ModelInstance::Initialize: 初期化済みです。");
		return false;
	}

	try {
		const std::size_t materialCount = model.GetMaterialCount();
		const std::size_t meshCount = model.GetMeshCount();

		// メッシュから参照する前に、材質の配列サイズを確定する
		materials_.resize(materialCount);
		materialData_.resize(materialCount);

		for (std::size_t i = 0; i < materialCount; ++i) {
			materialData_[i] = model.GetDefaultMaterialData(i);

			if (!materials_[i].Initialize(device,
			                              model.GetDefaultMaterial(i)) ||
			    !materials_[i].Update(materialData_[i])) {
				Logger::Error(std::format(
				    "ModelInstance: 材質{}の初期化に失敗しました。", i));

				Finalize();
				return false;
			}
		}

		meshInstances_.resize(meshCount);

		for (std::size_t i = 0; i < meshCount; ++i) {
			const std::size_t materialIndex = model.GetMeshMaterialIndex(i);

			if (materialIndex >= materials_.size()) {
				Logger::Error(std::format(
				    "ModelInstance: メッシュ{}の材質番号が範囲外です。", i));

				Finalize();
				return false;
			}

			if (!meshInstances_[i].Initialize(device, model.GetMesh(i),
			                                  materials_[materialIndex])) {
				Logger::Error(std::format(
				    "ModelInstance: メッシュ{}の描画準備に失敗しました。", i));

				Finalize();
				return false;
			}
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format(
		    "ModelInstanceの初期化中に例外が発生しました: {}", error.what()));

		Finalize();
		return false;
	}

	model_ = &model;

	Logger::Info("ModelInstanceを初期化しました。");
	return true;
}

bool ModelInstance::Update(const Camera &camera, const ColorTint &tint) {
	if (!model_) {
		Logger::Error("ModelInstance::Update: 未初期化です。");
		return false;
	}

	if (!TransformUtils::IsValid(transform_)) {
		Logger::Error("ModelInstance::Update: 変形情報が無効です。");
		return false;
	}

	if (!tint.IsValid()) {
		Logger::Error("ModelInstance::Update: 色の設定が無効です。");
		return false;
	}

	for (std::size_t i = 0; i < materials_.size(); ++i) {
		// 元の材質を保持し、GPUへ渡す今回の値にだけ色を掛ける。
		auto material = materialData_[i];
		material.baseColor.x *= tint.color.x;
		material.baseColor.y *= tint.color.y;
		material.baseColor.z *= tint.color.z;

		if (!materials_[i].Update(material)) {
			Logger::Error(
			    std::format("ModelInstance: 材質{}の更新に失敗しました。", i));
			return false;
		}
	}

	// モデルを構成する全メッシュに、同じ変形を適用する
	for (auto &instance : meshInstances_) {
		instance.SetTransform(transform_);
		instance.Update(camera);
	}

	return true;
}

void ModelInstance::Draw(ID3D12GraphicsCommandList *commandList,
                         MeshRenderer &renderer,
                         const DirectionalLight &light) const {
	if (!commandList || !model_) {
		Logger::Error("ModelInstance::Draw: 引数が無効、または未初期化です。");
		return;
	}

	for (const auto &instance : meshInstances_) {
		renderer.Draw(commandList, instance, light);
	}
}

void ModelInstance::SetTransform(const Transform &transform) {
	transform_ = transform;
}

const Transform &ModelInstance::GetTransform() const {
	return transform_;
}

std::size_t ModelInstance::GetMaterialCount() const {
	return materials_.size();
}

MaterialData &ModelInstance::GetMaterialData(std::size_t materialIndex) {
	return materialData_.at(materialIndex);
}

const MaterialData &
ModelInstance::GetMaterialData(std::size_t materialIndex) const {
	return materialData_.at(materialIndex);
}

void ModelInstance::Finalize() {
	// 材質を参照しているメッシュの描画情報から解放する
	for (auto &instance : meshInstances_) {
		instance.Finalize();
	}

	meshInstances_.clear();

	for (auto &material : materials_) {
		material.Finalize();
	}

	materials_.clear();
	materialData_.clear();

	model_ = nullptr;
	transform_ = {};
}

bool ModelInstance::SetMaterial(
    std::size_t slot, const std::shared_ptr<const MaterialResource> &resource) {
	if (!model_ || slot >= materials_.size()) {
		return false;
	}

	const auto &next = resource ? resource : model_->GetDefaultMaterial(slot);

	if (materials_[slot].GetResource() == next) {
		return true;
	}

	if (!materials_[slot].SetResource(next)) {
		return false;
	}

	materialData_[slot] = next->GetData();
	return true;
}
