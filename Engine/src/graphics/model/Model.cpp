#include "graphics/model/Model.h"
#include "animation/ModelAnimationData.h"
#include "core/Logger.h"
#include "graphics/model/ModelData.h"
#include "math/MathUtils.h"
#include "math/Ray.h"

#include <directxtk12/DirectXHelpers.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <format>

bool Model::Initialize(ID3D12Device *device, ID3D12CommandQueue *commandQueue,
                       const ModelData &data) {
	if (!device || !commandQueue || data.meshes.empty() ||
	    data.materials.empty()) {
		Logger::Error("Model::Initialize: 引数が無効です。");
		return false;
	}

	if (!parts_.empty() || !defaultMaterials_.empty()) {
		Logger::Error("Model::Initialize: 初期化済みです。");
		return false;
	}

	if (commandQueue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
		Logger::Error(
		    "Model::Initialize: DIRECT型のコマンドキューが必要です。");
		return false;
	}

	try {
		if (data.animation) {
			std::string error;
			if (!data.animation->IsValid(error)) {
				Logger::Error(error);
				return false;
			}
		}
		// 既定材質も独立した共有リソースとして作成し、モデルは参照だけを持つ。
		defaultMaterials_.reserve(data.materials.size());
		materialNames_.reserve(data.materials.size());
		materialMeshCounts_.assign(data.materials.size(), 0);

		for (const auto &source : data.materials) {
			materialNames_.push_back(source.name);
			MaterialData material;
			material.baseColor = source.baseColor;
			auto resource = std::make_shared<MaterialResource>();

			if (!resource->Initialize(device, commandQueue, material,
			                          source.baseColorTexture)) {
				Finalize();
				return false;
			}

			defaultMaterials_.push_back(std::move(resource));
		}

		if (!CreateMeshes(device, data)) {
			Finalize();
			return false;
		}
	} catch (const std::exception &error) {
		Logger::Error(std::format("Modelの初期化中に例外が発生しました: {}",
		                          error.what()));

		Finalize();
		return false;
	}

	animation_ = data.animation;
	try {
		if (!CreateAnimationMeshes(device)) {
			Finalize();
			return false;
		}
	} catch (const std::exception &e) {
		Logger::Error(e.what());
		Finalize();
		return false;
	}

	Logger::Info(std::format("Modelを初期化しました。メッシュ数={}、材質数={}",
	                         GetMeshCount(), GetMaterialCount()));

	return true;
}

bool Model::CreateMeshes(ID3D12Device *device, const ModelData &data) {
	parts_.resize(data.meshes.size());

	for (std::size_t i = 0; i < data.meshes.size(); ++i) {
		const ModelMeshData &source = data.meshes[i];

		if (source.materialIndex >= data.materials.size()) {
			Logger::Error(
			    std::format("Model: メッシュ{}の材質番号が範囲外です。", i));
			return false;
		}

		MeshPart &part = parts_[i];

		if (!part.mesh.Initialize(device, source.mesh.vertices,
		                          source.mesh.indices)) {
			Logger::Error(
			    std::format("Model: メッシュ{}の作成に失敗しました。", i));
			return false;
		}

		part.materialIndex = source.materialIndex;
		++materialMeshCounts_[part.materialIndex];

		// 選択判定には座標と頂点番号だけを保持する
		part.positions.reserve(source.mesh.vertices.size());

		for (const auto &vertex : source.mesh.vertices) {
			if (!MathUtils::IsFinite(vertex.position)) {
				Logger::Error(
				    std::format("Model: メッシュ{}の頂点座標が無効です。", i));
				return false;
			}

			part.positions.push_back(vertex.position);
		}

		part.indices = source.mesh.indices;

		DirectX::BoundingBox::CreateFromPoints(
		    part.bounds, part.positions.size(), part.positions.data(),
		    sizeof(DirectX::XMFLOAT3));

		// 最初のメッシュを基準に、モデル全体の範囲を広げる
		if (i == 0) {
			bounds_ = part.bounds;
		} else {
			DirectX::BoundingBox merged;

			DirectX::BoundingBox::CreateMerged(merged, bounds_, part.bounds);

			bounds_ = merged;
		}
	}

	return true;
}

const std::shared_ptr<const ModelAnimationData> &
Model::GetAnimationData() const {
	return animation_;
}

std::size_t Model::GetMeshCount() const {
	return parts_.size();
}

const Mesh &Model::GetMesh(std::size_t index) const {
	return parts_.at(index).mesh;
}

std::size_t Model::GetMeshMaterialIndex(std::size_t meshIndex) const {
	return parts_.at(meshIndex).materialIndex;
}

std::size_t Model::GetMaterialCount() const {
	return defaultMaterials_.size();
}

std::string_view Model::GetMaterialName(std::size_t materialIndex) const {
	return materialIndex < materialNames_.size()
	           ? std::string_view{materialNames_[materialIndex]}
	           : std::string_view{};
}

std::size_t Model::GetMaterialMeshCount(std::size_t materialIndex) const {
	return materialIndex < materialMeshCounts_.size()
	           ? materialMeshCounts_[materialIndex]
	           : 0;
}

const MaterialData &
Model::GetDefaultMaterialData(std::size_t materialIndex) const {
	return defaultMaterials_.at(materialIndex)->GetData();
}

const std::shared_ptr<const MaterialResource> &
Model::GetDefaultMaterial(std::size_t materialIndex) const {
	return defaultMaterials_.at(materialIndex);
}

bool Model::Raycast(const Ray &ray, float &distance) const {
	if (!MathUtils::IsFinite(ray.origin) ||
	    !MathUtils::IsFinite(ray.direction) ||
	    !std::isfinite(ray.maxDistance) || ray.maxDistance <= 0.0f) {
		return false;
	}

	using namespace DirectX;

	const XMVECTOR origin = XMLoadFloat3(&ray.origin);
	const XMVECTOR rawDirection = XMLoadFloat3(&ray.direction);

	const float lengthSquared = XMVectorGetX(XMVector3LengthSq(rawDirection));

	if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0f) {
		return false;
	}

	const XMVECTOR direction = XMVector3Normalize(rawDirection);

	float nearestDistance = ray.maxDistance;
	bool hit = false;

	for (const auto &part : parts_) {
		float boxDistance = 0.0f;

		// 外接ボックスに当たらないメッシュは三角形を調べない
		if (!part.bounds.Intersects(origin, direction, boxDistance)) {
			continue;
		}

		for (std::size_t i = 0; i + 2 < part.indices.size(); i += 3) {
			const auto &p0 = part.positions[part.indices[i]];
			const auto &p1 = part.positions[part.indices[i + 1]];
			const auto &p2 = part.positions[part.indices[i + 2]];

			float triangleDistance = 0.0f;

			if (TriangleTests::Intersects(origin, direction, XMLoadFloat3(&p0),
			                              XMLoadFloat3(&p1), XMLoadFloat3(&p2),
			                              triangleDistance) &&
			    std::isfinite(triangleDistance) && triangleDistance >= 0.0f &&
			    triangleDistance < nearestDistance) {
				nearestDistance = triangleDistance;
				hit = true;
			}
		}
	}

	if (hit) {
		distance = nearestDistance;
	}

	return hit;
}

bool Model::GetBoundsCorners(std::array<DirectX::XMFLOAT3, 8> &corners) const {
	if (parts_.empty()) {
		return false;
	}

	bounds_.GetCorners(corners.data());
	return true;
}

void Model::Finalize() {
	for (auto &part : parts_) {
		part.mesh.Finalize();
	}

	parts_.clear();
	animation_.reset();
	for (auto &part : animationParts_)
		part.mesh.Finalize();
	animationParts_.clear();

	bounds_ = {};

	defaultMaterials_.clear();
	materialNames_.clear();
	materialMeshCounts_.clear();
}
