#pragma once

#include "gameplay/CharacterSettings.h"
#include "gameplay/FollowCameraSettings.h"
#include "gameplay/RespawnSettings.h"
#include "graphics/material/ColorTint.h"
#include "math/Transform.h"
#include "physics/BoxCollider.h"
#include "scene/CameraComponent.h"
#include "scene/MeshComponent.h"
#include "scene/MeshRendererComponent.h"
#include "scene/SceneObjectId.h"

#include <filesystem>
#include <optional>
#include <string>

/// <summary>
/// シーン上の物体の名前・変形情報・描画と衝突の設定を保持する
/// </summary>
struct SceneObject {
	std::string name = "Object";
	// 自身の設定。実際の有効状態はSceneActivityで祖先と合わせて判定する
	bool active = true;
	// 親の変更にはSceneHierarchy::SetParentを使う。0はルートを表す
	SceneObjectId parentId = 0;
	// 親に対するローカル変形。ルートの場合はワールド変形と一致する
	Transform transform{};

	std::optional<MeshComponent> mesh;
	std::optional<MeshRendererComponent> meshRenderer;

	// 指定時は、モデルの全材質のRGBへこの色を掛ける
	std::optional<ColorTint> colorTint;

	// 指定した物体は衝突判定に使用する。未指定の場合は衝突形状を持たない
	std::optional<BoxCollider> collider;

	// 再生時に入力・重力で動かす物体。衝突形状も必要
	std::optional<CharacterSettings> character;
	// Transformを描画カメラとして使う設定
	std::optional<CameraComponent> camera;
	// カメラの追従動作。対象IDは同じシーンの物体を指定する
	std::optional<FollowCameraSettings> followCamera;
	// 落下時の復帰。Character Controllerを持つ物体に使用する
	std::optional<RespawnSettings> respawn;

	/// <summary>
	/// Meshに形状の参照が割り当てられているか確認する
	/// </summary>
	/// <returns>true:参照あり false:コンポーネントなし、または未割り当て</returns>
	bool HasMesh() const {
		return mesh && !mesh->modelPath.empty();
	}

	/// <summary>
	/// Mesh Rendererで表示を無効にしているか確認する
	/// </summary>
	/// <returns>true:非表示 false:表示が有効、またはMesh Rendererなし</returns>
	bool IsHidden() const {
		return meshRenderer && !meshRenderer->visible;
	}

	/// <summary>
	/// MeshとMesh Rendererがそろい、形状を描画できる設定か確認する
	/// </summary>
	/// <returns>true:描画対象 false:コンポーネント・形状の未設定、または非表示</returns>
	bool IsRenderable() const {
		return HasMesh() && meshRenderer && meshRenderer->visible;
	}
};
