#pragma once

#include "graphics/mesh/MeshData.h"
#include "math/Transform.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

/// <summary>
/// 骨とメッシュの親子関係。親は必ず自身より前に置き、ルートの親は-1とする
/// </summary>
struct ModelAnimationNode {
	std::string name;
	int parent = -1;
	DirectX::XMFLOAT4X4 localBind{};
	Transform bindTransform;
	bool hasBindTransform = false;
};

/// <summary>
/// メッシュ座標から、参照する骨のバインド座標へ変換する情報
/// </summary>
struct ModelBone {
	std::uint32_t node = 0;
	DirectX::XMFLOAT4X4 inverseBind{};
};

/// <summary>
/// 1頂点に影響する、同じメッシュ内の骨番号と正規化済みの重み
/// </summary>
struct ModelVertexWeights {
	static constexpr std::size_t kMaximumInfluences = 8;
	std::array<std::uint32_t, kMaximumInfluences> bones{};
	std::array<float, kMaximumInfluences> weights{};
};

/// <summary>
/// ノードへ結び付く元のメッシュ。重みがない頂点は、そのノードの姿勢に従う
/// </summary>
struct ModelAnimationMesh {
	MeshData mesh;
	std::uint32_t node = 0, materialIndex = 0;
	std::vector<ModelBone> bones;
	std::vector<ModelVertexWeights> weights;
};

/// <summary>
/// 位置・拡縮のキー。stepは次のキーまで現在値を保持する指定
/// </summary>
struct ModelVectorKey {
	double seconds = 0;
	DirectX::XMFLOAT3 value{};
	bool step = false;
};

/// <summary>
/// 単位クォータニオンのキー。step以外は最短経路の球面補間を使う
/// </summary>
struct ModelRotationKey {
	double seconds = 0;
	DirectX::XMFLOAT4 value{0, 0, 0, 1};
	bool step = false;
};

/// <summary>
/// 1ノードの位置・回転・拡縮。キーがない成分にはバインド姿勢を使う
/// </summary>
struct ModelAnimationTrack {
	enum class Boundary { DEFAULT, CONSTANT, LINEAR, REPEAT };
	std::uint32_t node = 0;
	Boundary before = Boundary::CONSTANT, after = Boundary::CONSTANT;
	std::vector<ModelVectorKey> positions, scales;
	std::vector<ModelRotationKey> rotations;
};

/// <summary>
/// 秒へ変換済みの、モデル内の名前付きアニメーションクリップ
/// </summary>
struct ModelAnimationClip {
	std::string name;
	double duration = 0;
	std::vector<ModelAnimationTrack> tracks;
};

/// <summary>
/// ファイルを閉じた後も保持する、骨・未変形メッシュ・再生クリップの共有データ
/// </summary>
struct ModelAnimationData {
	static constexpr std::size_t kMaximumNodes = 4096;
	static constexpr std::size_t kMaximumMeshes = 1024;
	static constexpr std::size_t kMaximumBones = 256;
	static constexpr std::size_t kMaximumClips = 128;
	static constexpr std::size_t kMaximumKeys = 1000000;
	static constexpr std::size_t kMaximumVertices = 1000000;
	static constexpr std::size_t kMaximumIndices = 3000000;
	static constexpr double kMaximumDuration = 3600;
	std::vector<ModelAnimationNode> nodes;
	std::vector<ModelAnimationMesh> meshes;
	std::vector<ModelAnimationClip> clips;

	/// <summary>
	/// 階層・形状・骨番号・重み・キーと、有限な行列を検証する
	/// </summary>
	/// <param name="error">成功時は空、失敗時は理由</param>
	/// <returns>true:有効 false:上限超過・不正な参照・補間できないキー</returns>
	bool IsValid(std::string &error) const;
};
