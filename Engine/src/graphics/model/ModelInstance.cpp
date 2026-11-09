#include "graphics/model/ModelInstance.h"
#include "core/Logger.h"
#include "graphics/DirectionalLight.h"
#include "graphics/PointLights.h"
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
	device_ = device;

	Logger::Info("ModelInstanceを初期化しました。");
	return true;
}

bool ModelInstance::Update(const Camera &camera,
                           const DirectX::XMFLOAT4X4 &world,
                           const ColorTint &tint) {
	if (!model_) {
		Logger::Error("ModelInstance::Update: 未初期化です。");
		return false;
	}

	DirectX::XMFLOAT4X4 inverse;

	if (!TransformUtils::TryInverseWorldMatrix(world, inverse)) {
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
	for (auto &instance :
	     animationPose_ ? animatedInstances_ : meshInstances_) {
		if (!instance.Update(camera, world)) {
			return false;
		}
	}

	return true;
}

void ModelInstance::Draw(ID3D12GraphicsCommandList *commandList,
                         MeshRenderer &renderer, const DirectionalLight &light,
                         const PointLights &pointLights,
                         const SpotLights &spotLights,
                         const ShadowMap &shadows) const {
	if (!commandList || !model_) {
		Logger::Error("ModelInstance::Draw: 引数が無効、または未初期化です。");
		return;
	}

	for (const auto &instance : GetMeshes()) {
		renderer.Draw(commandList, instance, light, pointLights, spotLights,
		              shadows);
	}
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

	for (auto &instance : animatedInstances_)
		instance.Finalize();
	animatedInstances_.clear();
	animationPose_.reset();

	for (auto &material : materials_) {
		material.Finalize();
	}

	materials_.clear();
	materialData_.clear();

	model_ = nullptr;
	device_ = nullptr;
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

std::span<const MeshInstance> ModelInstance::GetMeshes() const {
	return animationPose_ ? std::span<const MeshInstance>{animatedInstances_}
	                      : std::span<const MeshInstance>{meshInstances_};
}

bool ModelInstance::SetAnimationPose(const ModelAnimationPose *pose) {
	if (!model_)
		return false;
	if (!pose) {
		animationPose_.reset();
		return true;
	}

	const auto &data = model_->GetAnimationData();
	if (!data)
		return false;
	std::array<DirectX::XMFLOAT3, 8> bounds;
	if (!model_->GetAnimationBounds(*pose, bounds))
		return false;
	try {
		// 複製と検証を先に済ませ、無効な入力で以前のGPU行列を上書きしない。
		std::optional<ModelAnimationPose> next{*pose};

		if (animatedInstances_.empty()) {
			std::vector<MeshInstance> instances(data->meshes.size());
			for (std::size_t i = 0; i < instances.size(); ++i)
				if (!instances[i].Initialize(
				        device_, model_->GetAnimationMesh(i),
				        materials_.at(data->meshes[i].materialIndex),
				        &model_->GetAnimationWeights(i)))
					return false;
			animatedInstances_ = std::move(instances);
		}

		for (std::size_t i = 0; i < animatedInstances_.size(); ++i) {
			SkinningConstants constants;
			constants.boneCount =
			    static_cast<std::uint32_t>(data->meshes[i].bones.size());
			for (std::size_t b = 0; b <= constants.boneCount; ++b) {
				const auto &m = b < constants.boneCount
				                    ? pose->bones[i][b]
				                    : pose->nodes[data->meshes[i].node];
				DirectX::XMFLOAT4X4 inverse;
				if (!TransformUtils::TryInverseWorldMatrix(m, inverse))
					return false;
				constants.transforms[b] = m;
				DirectX::XMStoreFloat4x4(
				    &constants.normals[b],
				    DirectX::XMMatrixTranspose(
				        DirectX::XMLoadFloat4x4(&inverse)));
			}

			if (!animatedInstances_[i].UpdateSkinning(constants))
				return false;
		}

		animationPose_ = std::move(next);
		return true;
	} catch (const std::exception &e) {
		Logger::Error(e.what());
		return false;
	}
}

bool ModelInstance::GetBoundsCorners(
    std::array<DirectX::XMFLOAT3, 8> &corners) const {
	return model_ && (animationPose_
	                      ? model_->GetAnimationBounds(*animationPose_, corners)
	                      : model_->GetBoundsCorners(corners));
}

bool ModelInstance::Raycast(const Ray &ray, float &distance) const {
	return model_ && (animationPose_ ? model_->RaycastAnimation(*animationPose_,
	                                                            ray, distance)
	                                 : model_->Raycast(ray, distance));
}
