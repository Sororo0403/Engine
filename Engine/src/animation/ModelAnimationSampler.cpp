#include "animation/ModelAnimationSampler.h"
#include "math/MathUtils.h"
#include "math/TransformUtils.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

namespace {
/// <summary>
/// キーの外側の再生方式を反映し、端点または補間区間の値を取得する
/// </summary>
/// <param name="keys">昇順の同じ成分のキー</param>
/// <param name="time">クリップ内の秒数</param>
/// <param name="fallback">既定姿勢の成分</param>
/// <param name="track">キーの前後の方式</param>
/// <param name="blend">区間内の補間処理</param>
/// <returns>評価した成分</returns>
template <class Key, class Value, class Blend>
Value Evaluate(const std::vector<Key> &keys, double time, const Value &fallback,
               const ModelAnimationTrack &track, Blend blend) {
	if (keys.empty())
		return fallback;
	if (time < keys.front().seconds || time > keys.back().seconds) {
		const auto mode =
		    time < keys.front().seconds ? track.before : track.after;
		if (mode == ModelAnimationTrack::Boundary::DEFAULT)
			return fallback;
		if (mode == ModelAnimationTrack::Boundary::CONSTANT || keys.size() == 1)
			return time < keys.front().seconds ? keys.front().value
			                                   : keys.back().value;
		if (mode == ModelAnimationTrack::Boundary::REPEAT) {
			const double span = keys.back().seconds - keys.front().seconds;
			time = std::fmod(time - keys.front().seconds, span);
			if (time < 0)
				time += span;
			time += keys.front().seconds;
		}
	}
	if (keys.size() == 1)
		return keys[0].value;
	auto next = std::upper_bound(
	    keys.begin(), keys.end(), time,
	    [](double t, const Key &key) { return t < key.seconds; });
	if (next == keys.begin())
		++next;
	if (next == keys.end())
		--next;
	const auto &a = *(next - 1), &b = *next;
	if (a.step && time < b.seconds)
		return a.value;
	return blend(
	    a.value, b.value,
	    static_cast<float>((time - a.seconds) / (b.seconds - a.seconds)));
}
} // namespace

bool ModelAnimationSampler::Initialize(
    std::shared_ptr<const ModelAnimationData> data, std::string &error) {
	if (!data) {
		error = "Model animation data is missing.";
		return false;
	}
	if (!data->IsValid(error))
		return false;
	data_ = std::move(data);
	return true;
}

bool ModelAnimationSampler::Sample(std::optional<std::size_t> clip,
                                   double seconds, bool loop,
                                   ModelAnimationPose &pose,
                                   std::string &error) const {
	try {
		using namespace DirectX;
		if (!data_ || !std::isfinite(seconds) || seconds < 0 ||
		    (clip && *clip >= data_->clips.size()))
			throw std::runtime_error(
			    "Model animation sample arguments are invalid.");
		std::vector<XMFLOAT4X4> local;
		local.reserve(data_->nodes.size());
		for (const auto &node : data_->nodes)
			local.push_back(node.localBind);
		if (clip) {
			const auto &animation = data_->clips[*clip];
			const double time = loop ? std::fmod(seconds, animation.duration)
			                         : (std::min)(seconds, animation.duration);
			for (const auto &track : animation.tracks) {
				Transform t = data_->nodes[track.node].bindTransform;
				const auto vectorBlend = [](const XMFLOAT3 &a,
				                            const XMFLOAT3 &b, float factor) {
					XMFLOAT3 result;
					XMStoreFloat3(&result,
					              XMVectorLerp(XMLoadFloat3(&a),
					                           XMLoadFloat3(&b), factor));
					return result;
				};
				t.translation = Evaluate(track.positions, time, t.translation,
				                         track, vectorBlend);
				t.scale =
				    Evaluate(track.scales, time, t.scale, track, vectorBlend);
				t.rotation = Evaluate(
				    track.rotations, time, t.rotation, track,
				    [](const XMFLOAT4 &a, const XMFLOAT4 &b, float factor) {
					    XMFLOAT4 result;
					    XMStoreFloat4(&result, XMQuaternionSlerp(
					                               XMLoadFloat4(&a),
					                               XMLoadFloat4(&b), factor));
					    return result;
				    });
				if (!TransformUtils::TryNormalizeRotation(t.rotation,
				                                          t.rotation) ||
				    !TransformUtils::IsValid(t))
					throw std::runtime_error(
					    "Model animation transform became invalid.");
				XMStoreFloat4x4(&local[track.node],
				                TransformUtils::MakeWorldMatrix(t));
			}
		}
		ModelAnimationPose result;
		result.nodes.resize(local.size());
		for (std::size_t i = 0; i < local.size(); ++i) {
			auto matrix = XMLoadFloat4x4(&local[i]);
			const int parent = data_->nodes[i].parent;
			if (parent >= 0)
				matrix = matrix * XMLoadFloat4x4(&result.nodes[parent]);
			XMStoreFloat4x4(&result.nodes[i], matrix);
			if (!MathUtils::IsFinite(result.nodes[i]))
				throw std::runtime_error(
				    "Model animation hierarchy became non-finite.");
		}
		result.bones.resize(data_->meshes.size());
		for (std::size_t i = 0; i < data_->meshes.size(); ++i)
			for (const auto &bone : data_->meshes[i].bones) {
				XMFLOAT4X4 matrix;
				XMStoreFloat4x4(&matrix,
				                XMLoadFloat4x4(&bone.inverseBind) *
				                    XMLoadFloat4x4(&result.nodes[bone.node]));
				if (!MathUtils::IsFinite(matrix))
					throw std::runtime_error(
					    "Model skin matrix became non-finite.");
				result.bones[i].push_back(matrix);
			}
		pose = std::move(result);
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}

bool ModelAnimationSampler::Deform(std::size_t meshIndex,
                                   const ModelAnimationPose &pose,
                                   MeshData &mesh, std::string &error) const {
	try {
		using namespace DirectX;
		if (!data_ || meshIndex >= data_->meshes.size() ||
		    pose.nodes.size() != data_->nodes.size() ||
		    pose.bones.size() != data_->meshes.size())
			throw std::runtime_error("Model skin arguments are invalid.");
		const auto &source = data_->meshes[meshIndex];
		if (pose.bones[meshIndex].size() != source.bones.size())
			throw std::runtime_error("Model skin palette size is invalid.");
		std::vector<XMFLOAT4X4> normalMatrices;
		normalMatrices.reserve(source.bones.size() + 1);
		const auto normal = [&](const XMFLOAT4X4 &matrix) {
			XMFLOAT4X4 inverse;
			if (!TransformUtils::TryInverseWorldMatrix(matrix, inverse))
				throw std::runtime_error("Model skin matrix is singular.");
			XMFLOAT4X4 out;
			XMStoreFloat4x4(&out, XMMatrixTranspose(XMLoadFloat4x4(&inverse)));
			normalMatrices.push_back(out);
		};
		for (const auto &bone : pose.bones[meshIndex])
			normal(bone);
		normal(pose.nodes[source.node]);
		MeshData result = source.mesh;
		for (std::size_t i = 0; i < result.vertices.size(); ++i) {
			auto &v = result.vertices[i];
			const auto &p = source.mesh.vertices[i];
			auto position = XMVectorZero(), direction = XMVectorZero();
			float sum = 0;
			for (std::size_t j = 0; j < ModelVertexWeights::kMaximumInfluences;
			     ++j) {
				const float w = source.weights[i].weights[j];
				if (w == 0)
					continue;
				const auto bone = source.weights[i].bones[j];
				position = XMVectorAdd(
				    position,
				    XMVectorScale(
				        XMVector3TransformCoord(
				            XMLoadFloat3(&p.position),
				            XMLoadFloat4x4(&pose.bones[meshIndex][bone])),
				        w));
				direction = XMVectorAdd(
				    direction,
				    XMVectorScale(XMVector3TransformNormal(
				                      XMLoadFloat3(&p.normal),
				                      XMLoadFloat4x4(&normalMatrices[bone])),
				                  w));
				sum += w;
			}
			if (sum == 0) {
				position = XMVector3TransformCoord(
				    XMLoadFloat3(&p.position),
				    XMLoadFloat4x4(&pose.nodes[source.node]));
				direction = XMVector3TransformNormal(
				    XMLoadFloat3(&p.normal),
				    XMLoadFloat4x4(&normalMatrices.back()));
			}
			XMStoreFloat3(&v.position, position);
			const float length = XMVectorGetX(XMVector3LengthSq(direction));
			if (!std::isfinite(length) || length <= 0)
				throw std::runtime_error("Model skin normal became invalid.");
			XMStoreFloat3(&v.normal, XMVector3Normalize(direction));
			if (!MathUtils::IsFinite(v.position) ||
			    !MathUtils::IsFinite(v.normal))
				throw std::runtime_error(
				    "Model skin vertex became non-finite.");
		}
		mesh = std::move(result);
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}
