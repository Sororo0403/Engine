#include "animation/ModelAnimationSampler.h"
#include "core/Logger.h"
#include "graphics/model/Model.h"
#include "math/MathUtils.h"
#include "math/Ray.h"
#include "math/TransformUtils.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <directxtk12/BufferHelpers.h>
#include <exception>
#include <limits>

bool Model::CreateAnimationMeshes(ID3D12Device *device) {
	if (!animation_)
		return true;
	animationParts_.resize(animation_->meshes.size());

	for (std::size_t i = 0; i < animationParts_.size(); ++i) {
		auto &part = animationParts_[i];
		const auto &source = animation_->meshes[i];
		if (!part.mesh.Initialize(device, source.mesh.vertices,
		                          source.mesh.indices) ||
		    FAILED(DirectX::CreateUploadBuffer(device, source.weights.data(),
		                                       source.weights.size(),
		                                       part.weights.GetAddressOf())))
			return false;
		part.weightsView = {part.weights->GetGPUVirtualAddress(),
		                    static_cast<UINT>(source.weights.size() *
		                                      sizeof(ModelVertexWeights)),
		                    sizeof(ModelVertexWeights)};
		part.bounds.resize(source.bones.size() + 1);
		const auto extend = [&](std::size_t group,
		                        const DirectX::XMFLOAT3 &point) {
			DirectX::BoundingBox box{point, {0, 0, 0}};

			if (part.bounds[group]) {
				DirectX::BoundingBox merged;
				DirectX::BoundingBox::CreateMerged(merged, *part.bounds[group],
				                                   box);
				part.bounds[group] = merged;
			} else
				part.bounds[group] = box;
		};

		for (std::size_t v = 0; v < source.mesh.vertices.size(); ++v) {
			bool weighted = false;
			for (std::size_t w = 0; w < ModelVertexWeights::kMaximumInfluences;
			     ++w)
				if (source.weights[v].weights[w] > 0) {
					extend(source.weights[v].bones[w],
					       source.mesh.vertices[v].position);
					weighted = true;
				}
			if (!weighted)
				extend(source.bones.size(), source.mesh.vertices[v].position);
		}
	}

	return true;
}

const Mesh &Model::GetAnimationMesh(std::size_t index) const {
	return animationParts_.at(index).mesh;
}

const D3D12_VERTEX_BUFFER_VIEW &
Model::GetAnimationWeights(std::size_t index) const {
	return animationParts_.at(index).weightsView;
}

bool Model::GetAnimationBounds(
    const ModelAnimationPose &pose,
    std::array<DirectX::XMFLOAT3, 8> &corners) const {
	if (!animation_ || pose.nodes.size() != animation_->nodes.size() ||
	    pose.bones.size() != animationParts_.size())
		return false;
	using namespace DirectX;
	XMFLOAT3 low{FLT_MAX, FLT_MAX, FLT_MAX}, high{-FLT_MAX, -FLT_MAX, -FLT_MAX};
	bool found = false;

	for (std::size_t i = 0; i < animationParts_.size(); ++i) {
		const auto &part = animationParts_[i];
		const auto &source = animation_->meshes[i];
		if (pose.bones[i].size() != source.bones.size())
			return false;
		for (std::size_t group = 0; group < part.bounds.size(); ++group) {
			const auto &matrix = group < source.bones.size()
			                         ? pose.bones[i][group]
			                         : pose.nodes[source.node];
			XMFLOAT4X4 inverse;
			if (!TransformUtils::TryInverseWorldMatrix(matrix, inverse))
				return false;
			if (!part.bounds[group])
				continue;
			std::array<XMFLOAT3, 8> points;
			part.bounds[group]->GetCorners(points.data());

			for (auto point : points) {
				XMStoreFloat3(&point,
				              XMVector3TransformCoord(XMLoadFloat3(&point),
				                                      XMLoadFloat4x4(&matrix)));
				if (!MathUtils::IsFinite(point))
					return false;
				low.x = (std::min)(low.x, point.x);
				low.y = (std::min)(low.y, point.y);
				low.z = (std::min)(low.z, point.z);
				high.x = (std::max)(high.x, point.x);
				high.y = (std::max)(high.y, point.y);
				high.z = (std::max)(high.z, point.z);
				found = true;
			}
		}
	}

	if (!found)
		return false;
	// 正規化した重みの許容誤差と、CPU・GPUの浮動小数点の丸めを含める。
	const auto margin = [](float a, float b) {
		return .0002f * (std::max)({1.0f, std::abs(a), std::abs(b)});
	};
	const XMFLOAT3 padding{margin(low.x, high.x), margin(low.y, high.y),
	                       margin(low.z, high.z)};
	low = {low.x - padding.x, low.y - padding.y, low.z - padding.z};
	high = {high.x + padding.x, high.y + padding.y, high.z + padding.z};
	if (!MathUtils::IsFinite(low) || !MathUtils::IsFinite(high))
		return false;
	const XMFLOAT3 range[2]{low, high};
	BoundingBox bounds;
	BoundingBox::CreateFromPoints(bounds, 2, range, sizeof(XMFLOAT3));
	if (!MathUtils::IsFinite(bounds.Center) ||
	    !MathUtils::IsFinite(bounds.Extents))
		return false;
	bounds.GetCorners(corners.data());
	return true;
}

bool Model::RaycastAnimation(const ModelAnimationPose &pose, const Ray &ray,
                             float &distance) const {
	try {
		using namespace DirectX;
		if (!animation_ || !MathUtils::IsFinite(ray.origin) ||
		    !MathUtils::IsFinite(ray.direction) ||
		    !std::isfinite(ray.maxDistance) || ray.maxDistance <= 0)
			return false;
		const auto raw = XMLoadFloat3(&ray.direction);
		const float length = XMVectorGetX(XMVector3LengthSq(raw));
		if (!std::isfinite(length) || length <= 0)
			return false;
		std::array<XMFLOAT3, 8> corners;
		if (!GetAnimationBounds(pose, corners))
			return false;
		BoundingBox bounds;
		BoundingBox::CreateFromPoints(bounds, 8, corners.data(),
		                              sizeof(XMFLOAT3));
		const auto origin = XMLoadFloat3(&ray.origin),
		           direction = XMVector3Normalize(raw);
		float boxDistance = 0;
		if (!bounds.Intersects(origin, direction, boxDistance))
			return false;
		ModelAnimationSampler sampler;
		std::string error;
		if (!sampler.Initialize(animation_, error))
			return false;
		float nearest = ray.maxDistance;
		bool hit = false;

		for (std::size_t i = 0; i < animation_->meshes.size(); ++i) {
			MeshData mesh;
			if (!sampler.Deform(i, pose, mesh, error))
				return false;
			for (std::size_t j = 0; j < mesh.indices.size(); j += 3) {
				const auto &a = mesh.vertices[mesh.indices[j]].position;
				const auto &b = mesh.vertices[mesh.indices[j + 1]].position;
				const auto &c = mesh.vertices[mesh.indices[j + 2]].position;
				float t = 0;
				if (TriangleTests::Intersects(
				        origin, direction, XMLoadFloat3(&a), XMLoadFloat3(&b),
				        XMLoadFloat3(&c), t) &&
				    std::isfinite(t) && t >= 0 && t < nearest) {
					nearest = t;
					hit = true;
				}
			}
		}

		if (hit)
			distance = nearest;
		return hit;
	} catch (const std::exception &e) {
		Logger::Error(e.what());
		return false;
	}
}
