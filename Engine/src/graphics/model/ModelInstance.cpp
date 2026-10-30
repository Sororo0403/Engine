#include "graphics/model/ModelInstance.h"
#include "core/Logger.h"
#include "graphics/Camera.h"
#include "graphics/DirectionalLight.h"
#include "graphics/render/MeshRenderer.h"
#include "graphics/model/Model.h"

#include <cmath>
#include <exception>
#include <format>

bool ModelInstance::Initialize(ID3D12Device *device, const Model &model) {
	if (!device || model.GetMeshCount() == 0 || model.GetMaterialCount() == 0 ||
	    !model.GetDescriptorHeap()) {
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

			if (!materials_[i].Initialize(device, model.GetTextureSrv(i)) ||
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

bool ModelInstance::Update(const Camera &camera) {
	if (!model_) {
		Logger::Error("ModelInstance::Update: 未初期化です。");
		return false;
	}

	// 無限大やNaNを変換行列へ渡さない
	const float values[] = {transform_.scale.x,       transform_.scale.y,
	                        transform_.scale.z,       transform_.rotation.x,
	                        transform_.rotation.y,    transform_.rotation.z,
	                        transform_.translation.x, transform_.translation.y,
	                        transform_.translation.z};

	for (const float value : values) {
		if (!std::isfinite(value)) {
			Logger::Error("ModelInstance::Update: 変形情報が無効です。");
			return false;
		}
	}

	// 法線の変換に逆行列を使うため、拡大率0は禁止する
	if (transform_.scale.x == 0.0f || transform_.scale.y == 0.0f ||
	    transform_.scale.z == 0.0f) {
		Logger::Error("ModelInstance::Update: 拡大率に0は指定できません。");
		return false;
	}

	for (std::size_t i = 0; i < materials_.size(); ++i) {
		if (!materials_[i].Update(materialData_[i])) {
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

	ID3D12DescriptorHeap *heap = model_->GetDescriptorHeap();

	if (!heap) {
		Logger::Error("ModelInstance::Draw: モデルのSRVヒープがありません。");
		return;
	}

	ID3D12DescriptorHeap *heaps[] = {heap};
	commandList->SetDescriptorHeaps(1, heaps);

	for (const auto &instance : meshInstances_) {
		renderer.Draw(commandList, instance, light);
	}
}

void ModelInstance::SetTransform(const Transform &transform) {
	transform_ = transform;
}

const Transform &ModelInstance::GetTransform() const { return transform_; }

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
