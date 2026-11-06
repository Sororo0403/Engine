#include "animation/ModelAnimationData.h"
#include "math/MathUtils.h"
#include "math/TransformUtils.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <set>
#include <stdexcept>

namespace {
/// <summary>
/// 有限で逆変換できるアフィン行列を要求する
/// </summary>
/// <param name="matrix">検証する変換</param>
void RequireMatrix(const DirectX::XMFLOAT4X4 &matrix) {
	DirectX::XMFLOAT4X4 inverse;
	if (!TransformUtils::TryInverseWorldMatrix(matrix, inverse))
		throw std::runtime_error(
		    "Animation matrix must be finite, affine and invertible.");
}

/// <summary>
/// キーの上限・昇順・クリップ内の時刻・値を確認する
/// </summary>
/// <param name="keys">同じ成分のキー</param>
/// <param name="duration">クリップの秒数</param>
/// <param name="total">全クリップのキー数</param>
template <class Key>
void RequireKeys(const std::vector<Key> &keys, double duration,
                 std::size_t &total) {
	if (keys.size() > ModelAnimationData::kMaximumKeys - total)
		throw std::runtime_error("Model animation key limit exceeded.");
	total += keys.size();
	double prior = -1;
	for (const auto &key : keys) {
		if (!std::isfinite(key.seconds) || key.seconds < 0 ||
		    key.seconds > duration || key.seconds <= prior ||
		    !MathUtils::IsFinite(key.value))
			throw std::runtime_error("Animation keys must be finite and "
			                         "strictly ordered within the clip.");
		prior = key.seconds;
	}
}
} // namespace

bool ModelAnimationData::IsValid(std::string &error) const {
	try {
		if (nodes.empty() || nodes.size() > kMaximumNodes || meshes.empty() ||
		    meshes.size() > kMaximumMeshes || clips.size() > kMaximumClips)
			throw std::runtime_error(
			    "Model animation node, mesh or clip count is invalid.");
		for (std::size_t i = 0; i < nodes.size(); ++i) {
			const auto &node = nodes[i];
			if (node.parent < -1 ||
			    (node.parent >= 0 &&
			     static_cast<std::size_t>(node.parent) >= i))
				throw std::runtime_error(
				    "Animation parents must precede their children.");
			RequireMatrix(node.localBind);
			if (node.hasBindTransform &&
			    !TransformUtils::IsValid(node.bindTransform))
				throw std::runtime_error(
				    "Animation bind transform is invalid.");
		}
		std::size_t vertices = 0, indices = 0;
		for (const auto &part : meshes) {
			if (part.node >= nodes.size() ||
			    part.bones.size() > kMaximumBones ||
			    part.mesh.vertices.empty() || part.mesh.indices.empty() ||
			    part.mesh.indices.size() % 3 ||
			    part.weights.size() != part.mesh.vertices.size() ||
			    part.mesh.vertices.size() > kMaximumVertices - vertices ||
			    part.mesh.indices.size() > kMaximumIndices - indices)
				throw std::runtime_error(
				    "Animated mesh count or references are invalid.");
			vertices += part.mesh.vertices.size();
			indices += part.mesh.indices.size();
			for (const auto &bone : part.bones) {
				if (bone.node >= nodes.size())
					throw std::runtime_error("Animation bone node is missing.");
				RequireMatrix(bone.inverseBind);
			}
			for (auto index : part.mesh.indices)
				if (index >= part.mesh.vertices.size())
					throw std::runtime_error(
					    "Animated mesh index is out of range.");
			for (std::size_t i = 0; i < part.mesh.vertices.size(); ++i) {
				const auto &v = part.mesh.vertices[i];
				if (!MathUtils::IsFinite(v.position) ||
				    !MathUtils::IsFinite(v.normal) ||
				    !MathUtils::IsFinite(v.color) ||
				    !MathUtils::IsFinite(v.texcoord))
					throw std::runtime_error(
					    "Animated vertex contains non-finite data.");
				double sum = 0;
				std::set<std::uint32_t> used;
				for (std::size_t j = 0;
				     j < ModelVertexWeights::kMaximumInfluences; ++j) {
					const float w = part.weights[i].weights[j];
					const auto bone = part.weights[i].bones[j];
					if (!std::isfinite(w) || w < 0 || w > 1 ||
					    (w > 0 && (bone >= part.bones.size() ||
					               !used.insert(bone).second)))
						throw std::runtime_error(
						    "Animated vertex weights are invalid.");
					sum += w;
				}
				if (sum != 0 && std::abs(sum - 1) > .0001)
					throw std::runtime_error(
					    "Animated vertex weights must sum to one.");
			}
		}
		std::size_t total = 0;
		std::set<std::string> names;
		for (const auto &clip : clips) {
			if (clip.name.empty() || !names.insert(clip.name).second ||
			    !std::isfinite(clip.duration) || clip.duration <= 0 ||
			    clip.duration > kMaximumDuration || clip.tracks.empty() ||
			    clip.tracks.size() > nodes.size())
				throw std::runtime_error(
				    "Animation clip name, duration or tracks are invalid.");
			std::set<std::uint32_t> tracked;
			for (const auto &track : clip.tracks) {
				if (track.node >= nodes.size() ||
				    !nodes[track.node].hasBindTransform ||
				    !tracked.insert(track.node).second ||
				    track.before < ModelAnimationTrack::Boundary::DEFAULT ||
				    track.before > ModelAnimationTrack::Boundary::REPEAT ||
				    track.after < ModelAnimationTrack::Boundary::DEFAULT ||
				    track.after > ModelAnimationTrack::Boundary::REPEAT ||
				    (track.positions.empty() && track.scales.empty() &&
				     track.rotations.empty()))
					throw std::runtime_error(
					    "Animation track target or boundary mode is invalid.");
				RequireKeys(track.positions, clip.duration, total);
				RequireKeys(track.scales, clip.duration, total);
				RequireKeys(track.rotations, clip.duration, total);
				for (std::size_t i = 0; i < track.scales.size(); ++i) {
					const auto &s = track.scales[i].value;
					if (s.x == 0 || s.y == 0 || s.z == 0)
						throw std::runtime_error(
						    "Animated scale must be nonzero.");
					if (i && !track.scales[i - 1].step) {
						const auto &p = track.scales[i - 1].value;
						if (std::signbit(s.x) != std::signbit(p.x) ||
						    std::signbit(s.y) != std::signbit(p.y) ||
						    std::signbit(s.z) != std::signbit(p.z))
							throw std::runtime_error(
							    "Interpolated scale must not cross zero.");
					}
				}
				for (const auto &key : track.rotations) {
					const auto &q = key.value;
					const double length = double(q.x) * q.x +
					                      double(q.y) * q.y +
					                      double(q.z) * q.z + double(q.w) * q.w;
					if (std::abs(length - 1) > .0001)
						throw std::runtime_error(
						    "Animation rotations must be normalized.");
				}
			}
		}
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}
