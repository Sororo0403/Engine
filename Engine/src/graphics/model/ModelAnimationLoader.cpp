#include "ModelAnimationLoader.h"
#include "GltfAnimationMetadata.h"
#include "math/TransformUtils.h"
#include <algorithm>
#include <assimp/scene.h>
#include <cmath>
#include <exception>
#include <format>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace {
/// <summary>
/// Assimpの列ベクトル用行列を、同じ左手座標の行ベクトル用へ転置する
/// </summary>
/// <param name="m">座標系変換済みの行列</param>
/// <returns>DirectXMathで合成できる行列</returns>
DirectX::XMFLOAT4X4 Matrix(const aiMatrix4x4 &m) {
	return {m.a1, m.b1, m.c1, m.d1, m.a2, m.b2, m.c2, m.d2,
	        m.a3, m.b3, m.c3, m.d3, m.a4, m.b4, m.c4, m.d4};
}

/// <summary>
/// 元の前後補間方式を、Assimpに依存しない値へ変換する
/// </summary>
/// <param name="mode">Assimpの前後方式</param>
/// <returns>対応する方式。不明な指定は例外</returns>
ModelAnimationTrack::Boundary Boundary(aiAnimBehaviour mode) {
	using B = ModelAnimationTrack::Boundary;
	switch (mode) {
	case aiAnimBehaviour_DEFAULT:
		return B::DEFAULT;
	case aiAnimBehaviour_CONSTANT:
		return B::CONSTANT;
	case aiAnimBehaviour_LINEAR:
		return B::LINEAR;
	case aiAnimBehaviour_REPEAT:
		return B::REPEAT;
	default:
		throw std::runtime_error("Unsupported animation boundary mode.");
	}
}

/// <summary>
/// 接線を保持できない補間を線形として読み違えず、対応するキーだけを受け入れる
/// </summary>
/// <param name="mode">元のキーの補間</param>
/// <returns>true:保持 false:線形または球面補間。未対応は例外</returns>
bool Step(aiAnimInterpolation mode) {
	if (mode == aiAnimInterpolation_Step)
		return true;
	if (mode == aiAnimInterpolation_Linear ||
	    mode == aiAnimInterpolation_Spherical_Linear)
		return false;
	throw std::runtime_error(
	    "Model animation requires LINEAR or STEP keys; cubic spline tangents "
	    "are not retained by this importer.");
}
} // namespace

bool ModelAnimationLoader::Read(const aiScene &scene, ModelAnimationData &data,
                                std::string &error,
                                const GltfAnimationMetadata *metadata) {
	try {
		if (!scene.mRootNode ||
		    scene.mNumAnimations > ModelAnimationData::kMaximumClips ||
		    scene.mNumMeshes > ModelAnimationData::kMaximumMeshes)
			throw std::runtime_error(
			    "Animated model scene is missing or exceeds limits.");
		ModelAnimationData result;

		struct Entry {
			const aiNode *node;
			int parent;
			unsigned depth;
		};

		std::vector<Entry> pending{{scene.mRootNode, -1, 0}};
		std::vector<const aiNode *> sources;
		std::set<const aiNode *> visited;
		std::map<std::string, std::vector<std::uint32_t>> named;
		while (!pending.empty()) {
			const auto entry = pending.back();
			pending.pop_back();
			if (!entry.node || !visited.insert(entry.node).second ||
			    entry.depth > 256 ||
			    result.nodes.size() >= ModelAnimationData::kMaximumNodes)
				throw std::runtime_error(
				    "Animated model hierarchy is invalid or too large.");
			const auto index = static_cast<std::uint32_t>(result.nodes.size());
			const auto &source = *entry.node;
			ModelAnimationNode node;
			node.name = source.mName.C_Str();
			node.parent = entry.parent;
			node.localBind = Matrix(source.mTransformation);
			node.hasBindTransform = TransformUtils::TryFromWorldMatrix(
			    node.localBind, node.bindTransform);
			named[node.name].push_back(index);
			result.nodes.push_back(std::move(node));
			sources.push_back(entry.node);
			if (source.mNumChildren > ModelAnimationData::kMaximumNodes ||
			    pending.size() + source.mNumChildren >
			        ModelAnimationData::kMaximumNodes)
				throw std::runtime_error(
				    "Animated model child count exceeds the limit.");
			for (unsigned i = source.mNumChildren; i > 0; --i)
				pending.push_back({source.mChildren[i - 1],
				                   static_cast<int>(index), entry.depth + 1});
		}
		const auto findNode = [&](const aiString &name) {
			auto at = named.find(name.C_Str());
			if (at == named.end() || at->second.size() != 1)
				throw std::runtime_error(
				    std::format("Animation node must have a unique name: {}",
				                name.C_Str()));
			return at->second[0];
		};
		std::size_t vertexCount = 0, indexCount = 0;
		for (std::size_t node = 0; node < sources.size(); ++node) {
			const auto &owner = *sources[node];
			if (owner.mNumMeshes > ModelAnimationData::kMaximumMeshes)
				throw std::runtime_error(
				    "Animated node mesh count exceeds the limit.");
			for (unsigned mi = 0; mi < owner.mNumMeshes; ++mi) {
				if (owner.mMeshes[mi] >= scene.mNumMeshes)
					throw std::runtime_error("Animated node mesh is missing.");
				const auto &source = *scene.mMeshes[owner.mMeshes[mi]];
				if ((source.mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0)
					continue;
				if (result.meshes.size() >=
				        ModelAnimationData::kMaximumMeshes ||
				    !source.HasPositions() || !source.HasNormals() ||
				    !source.HasFaces() ||
				    source.mMaterialIndex >= scene.mNumMaterials ||
				    source.mNumBones > ModelAnimationData::kMaximumBones ||
				    source.mNumVertices >
				        ModelAnimationData::kMaximumVertices - vertexCount ||
				    source.mNumFaces >
				        (ModelAnimationData::kMaximumIndices - indexCount) / 3)
					throw std::runtime_error(
					    "Animated mesh data is invalid or exceeds limits.");
				vertexCount += source.mNumVertices;
				indexCount += std::size_t(source.mNumFaces) * 3;
				ModelAnimationMesh mesh;
				mesh.node = static_cast<std::uint32_t>(node);
				mesh.materialIndex = source.mMaterialIndex;
				mesh.mesh.vertices.resize(source.mNumVertices);
				mesh.weights.resize(source.mNumVertices);
				for (unsigned i = 0; i < source.mNumVertices; ++i) {
					auto &v = mesh.mesh.vertices[i];
					const auto &p = source.mVertices[i];
					const auto &n = source.mNormals[i];
					v.position = {p.x, p.y, p.z};
					v.normal = {n.x, n.y, n.z};
					v.color = {1, 1, 1, 1};
					if (source.HasTextureCoords(0)) {
						const auto &uv = source.mTextureCoords[0][i];
						v.texcoord = {uv.x, uv.y};
					}
					if (source.HasVertexColors(0)) {
						const auto &c = source.mColors[0][i];
						v.color = {c.r, c.g, c.b, c.a};
					}
				}
				mesh.mesh.indices.reserve(std::size_t(source.mNumFaces) * 3);
				for (unsigned i = 0; i < source.mNumFaces; ++i) {
					const auto &face = source.mFaces[i];
					if (face.mNumIndices != 3)
						throw std::runtime_error(
						    "Animated mesh must contain triangles.");
					for (unsigned k = 0; k < 3; ++k)
						mesh.mesh.indices.push_back(face.mIndices[k]);
				}
				for (unsigned b = 0; b < source.mNumBones; ++b) {
					const auto &bone = *source.mBones[b];
					mesh.bones.push_back(
					    {findNode(bone.mName), Matrix(bone.mOffsetMatrix)});
					if (bone.mNumWeights > source.mNumVertices)
						throw std::runtime_error(
						    "Bone weight count exceeds vertex count.");
					for (unsigned wi = 0; wi < bone.mNumWeights; ++wi) {
						const auto &w = bone.mWeights[wi];
						if (w.mVertexId >= mesh.weights.size() ||
						    !std::isfinite(w.mWeight) || w.mWeight < 0)
							throw std::runtime_error(
							    "Bone vertex weight is invalid.");
						if (w.mWeight == 0)
							continue;
						auto &weights = mesh.weights[w.mVertexId];
						std::size_t slot = 0;
						for (; slot < weights.weights.size(); ++slot)
							if (weights.weights[slot] == 0 ||
							    weights.bones[slot] == b)
								break;
						if (slot == weights.weights.size())
							throw std::runtime_error(
							    "A vertex may have at most eight bone "
							    "influences.");
						weights.bones[slot] = b;
						weights.weights[slot] += w.mWeight;
					}
				}
				for (auto &w : mesh.weights) {
					double sum = 0;
					for (float v : w.weights)
						sum += v;
					if (!std::isfinite(sum))
						throw std::runtime_error(
						    "Bone weight total is invalid.");
					if (sum > 0)
						for (auto &v : w.weights)
							v = static_cast<float>(v / sum);
				}
				result.meshes.push_back(std::move(mesh));
			}
		}
		std::size_t totalKeys = 0;
		std::set<std::string> clipNames;
		for (unsigned i = 0; i < scene.mNumAnimations; ++i) {
			const auto &a = *scene.mAnimations[i];
			if (a.mNumMeshChannels || a.mNumMorphMeshChannels)
				throw std::runtime_error("Morph and vertex animation are not "
				                         "supported by skeletal clips.");
			if (!std::isfinite(a.mDuration) || a.mDuration < 0 ||
			    !std::isfinite(a.mTicksPerSecond) || a.mTicksPerSecond < 0 ||
			    a.mNumChannels == 0 || a.mNumChannels > result.nodes.size())
				throw std::runtime_error(
				    "Animation duration, tick rate or channels are invalid.");
			const double ticks = a.mTicksPerSecond > 0 ? a.mTicksPerSecond : 25;
			double origin = 0, end = a.mDuration / ticks;
			for (unsigned c = 0; c < a.mNumChannels; ++c) {
				const auto &channel = *a.mChannels[c];
				const std::size_t count =
				    std::size_t(channel.mNumPositionKeys) +
				    channel.mNumRotationKeys + channel.mNumScalingKeys;
				if (count > ModelAnimationData::kMaximumKeys - totalKeys)
					throw std::runtime_error(
					    "Model animation key limit exceeded.");
				totalKeys += count;
				const auto range = [&](const auto *keys, unsigned n) {
					for (unsigned k = 0; k < n; ++k) {
						const double time = keys[k].mTime / ticks;
						if (!std::isfinite(time))
							throw std::runtime_error(
							    "Animation key time is invalid.");
						origin = (std::min)(origin, time);
						end = (std::max)(end, time);
					}
				};
				range(channel.mPositionKeys, channel.mNumPositionKeys);
				range(channel.mRotationKeys, channel.mNumRotationKeys);
				range(channel.mScalingKeys, channel.mNumScalingKeys);
			}
			ModelAnimationClip clip;
			const std::string base = a.mName.length
			                             ? a.mName.C_Str()
			                             : std::format("Animation {}", i + 1);
			clip.name = base;
			unsigned suffix = 2;
			while (!clipNames.insert(clip.name).second)
				clip.name = std::format("{} #{}", base, suffix++);
			clip.duration = end - origin;
			if (clip.duration == 0)
				clip.duration = 1 / ticks;
			for (unsigned c = 0; c < a.mNumChannels; ++c) {
				const auto &channel = *a.mChannels[c];
				ModelAnimationTrack track;
				track.node = findNode(channel.mNodeName);
				track.before = Boundary(channel.mPreState);
				track.after = Boundary(channel.mPostState);
				const auto vectors = [&](const aiVectorKey *keys,
				                         unsigned count,
				                         std::vector<ModelVectorKey> &out) {
					out.reserve(count);
					for (unsigned k = 0; k < count; ++k) {
						const auto &v = keys[k];
						out.push_back({v.mTime / ticks - origin,
						               {v.mValue.x, v.mValue.y, v.mValue.z},
						               Step(v.mInterpolation)});
					}
				};
				vectors(channel.mPositionKeys, channel.mNumPositionKeys,
				        track.positions);
				vectors(channel.mScalingKeys, channel.mNumScalingKeys,
				        track.scales);
				track.rotations.reserve(channel.mNumRotationKeys);
				for (unsigned k = 0; k < channel.mNumRotationKeys; ++k) {
					const auto &v = channel.mRotationKeys[k];
					DirectX::XMFLOAT4 q{v.mValue.x, v.mValue.y, v.mValue.z,
					                    v.mValue.w};
					if (!TransformUtils::TryNormalizeRotation(q, q))
						throw std::runtime_error(
						    "Animation quaternion is invalid.");
					track.rotations.push_back(
					    {v.mTime / ticks - origin, q, Step(v.mInterpolation)});
				}
				clip.tracks.push_back(std::move(track));
			}
			result.clips.push_back(std::move(clip));
		}
		if (metadata && !metadata->Apply(result, error))
			return false;
		if (!result.IsValid(error))
			return false;
		data = std::move(result);
		error.clear();
		return true;
	} catch (const std::exception &e) {
		error = e.what();
		return false;
	}
}
